#include "SSmvShell.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SmvDataService.h"
#include "SmvFlightLink.h"
#include "SmvLocalFlightLink.h"
#include "SmvMatchClient.h"
#include "SmvGameMode.h"
#include "SmvGameInstance.h"
#include "SmvSteam.h"
#include "SmvFabContent.h"
#include "SmvMenuScene.h"
#include "SmvUiPrefs.h"
#include "SmvI18n.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "SmvShell"

using namespace SmvUi;

namespace
{
	/** Slate bildirimini (SNew …) SWidget başvurusuna çevirir — üçlü işleçte iki dalın türü eşit olsun */
	template<typename T> TSharedRef<SWidget> AsW(TSharedRef<T> W) { return W; }

	/** Ayarlar → Görüntü düzenleme tamponu (GameUserSettings) */
	struct FSmvDisplayEdit
	{
		FString Res, Mode, Limit;
		bool bVsync = true;
		double Scale = 1.0;
		int32 View = 3, Shadow = 3, Texture = 3, Effects = 3, Post = 3, Aa = 3, Foliage = 3, Shading = 3;
		void Load()
		{
			UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr;
			if (!G) return;
			const FIntPoint R = G->GetScreenResolution();
			Res = FString::Printf(TEXT("%dx%d"), R.X, R.Y);
			const EWindowMode::Type M = G->GetFullscreenMode();
			Mode = M == EWindowMode::Fullscreen ? TEXT("full") : M == EWindowMode::WindowedFullscreen ? TEXT("borderless") : TEXT("window");
			const float L = G->GetFrameRateLimit();
			Limit = L <= 0.f ? TEXT("0") : FString::FromInt(FMath::RoundToInt(L));
			bVsync = G->IsVSyncEnabled();
			Scale = G->GetResolutionScaleNormalized();
			View = G->GetViewDistanceQuality(); Shadow = G->GetShadowQuality(); Texture = G->GetTextureQuality(); Effects = G->GetVisualEffectQuality();
			Post = G->GetPostProcessingQuality(); Aa = G->GetAntiAliasingQuality(); Foliage = G->GetFoliageQuality(); Shading = G->GetShadingQuality();
		}
	};
	FSmvDisplayEdit GSmvDisp;

	/** Web hangar tür istatistikleri (hız, menzil, dayanıklılık, çeviklik; 0–100) */
	void SmvTypeStats(const FString& T, float Out[4])
	{
		const float S[7][4] = { { 45, 40, 70, 65 }, { 70, 90, 60, 35 }, { 55, 50, 55, 60 }, { 75, 85, 65, 45 }, { 85, 25, 35, 100 }, { 100, 60, 40, 80 }, { 30, 45, 90, 50 } };
		const int32 I = T == TEXT("plane") ? 1 : T == TEXT("heli") ? 2 : T == TEXT("vtol") ? 3 : T == TEXT("fpv") ? 4 : T == TEXT("interceptor") ? 5 : T == TEXT("rover") ? 6 : 0;
		for (int32 k = 0; k < 4; ++k) Out[k] = S[I][k] / 100.f;
	}

	FLinearColor SmvHex(const FString& H, const FLinearColor& Def = FLinearColor::Gray)
	{
		return H.Len() == 7 ? FLinearColor::FromSRGBColor(FColor::FromHex(H)) : Def;
	}

	const TArray<FString>& SmvTypes()
	{
		static const TArray<FString> T = { TEXT("multirotor"), TEXT("fpv"), TEXT("plane"), TEXT("vtol"), TEXT("interceptor"), TEXT("heli"), TEXT("rover") };
		return T;
	}

	/** Fab haritası kategorisi (açıklama metni için): ruined | forest | city | refinery | military | generic */
	FString SmvMapCategory(const FString& Key)
	{
		const FString K = Key.ToLower();
		if (K.Contains(TEXT("ruin"))) return TEXT("ruined");
		if (K.Contains(TEXT("forest")) || K.Contains(TEXT("brushify")) || K.Contains(TEXT("orman"))) return TEXT("forest");
		if (K.Contains(TEXT("city")) || K.Contains(TEXT("urban")) || K.Contains(TEXT("sehir"))) return TEXT("city");
		if (K.Contains(TEXT("refin")) || K.Contains(TEXT("rafineri")) || K.Contains(TEXT("industr"))) return TEXT("refinery");
		if (K.Contains(TEXT("military")) || K.Contains(TEXT("base")) || K.Contains(TEXT("askeri"))) return TEXT("military");
		return TEXT("generic");
	}

	const TArray<FString>& SmvPalette()
	{
		static const TArray<FString> P = { TEXT("#e53935"), TEXT("#fb8c00"), TEXT("#fdd835"), TEXT("#43a047"), TEXT("#00acc1"), TEXT("#1e88e5"),
			TEXT("#5e35b1"), TEXT("#d81b60"), TEXT("#eceff1"), TEXT("#263238") };
		return P;
	}

	/** Harita önizleme görseli (kayıt tablosu PNG'si; bir kez yüklenir, ömür boyu tutulur) */
	const FSlateBrush* SmvMapPreview(const FSmvFabMap& M)
	{
		static TMap<FString, TSharedPtr<FSlateBrush>> Brushes;
		static TArray<TStrongObjectPtr<UTexture2D>> Keep;
		if (TSharedPtr<FSlateBrush>* B = Brushes.Find(M.Key)) return B->Get();
		TSharedPtr<FSlateBrush> Br;
		TArray<uint8> Bytes;
		if (!M.PreviewFile.IsEmpty() && FFileHelper::LoadFileToArray(Bytes, *M.PreviewFile))
		{
			if (UTexture2D* Tex = FImageUtils::ImportBufferAsTexture2D(Bytes))
			{
				Keep.Add(TStrongObjectPtr<UTexture2D>(Tex));
				Br = MakeShared<FSlateBrush>();
				Br->SetResourceObject(Tex);
				Br->ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
				Br->DrawAs = ESlateBrushDrawType::Image;
			}
		}
		Brushes.Add(M.Key, Br);
		return Br.Get();
	}

	const TArray<FString>& SmvSkins()
	{
		static const TArray<FString> P = { TEXT("#f9dcc4"), TEXT("#f1c27d"), TEXT("#e0ac69"), TEXT("#c68642"), TEXT("#a1665e"), TEXT("#8d5524"), TEXT("#6b4226"), TEXT("#3d2314") };
		return P;
	}
}

FText SSmvShell::Tx(const TCHAR* Key) { return FSmvI18n::T(Key); }

USmvDataService* SSmvShell::Data() const { return Flow.IsValid() ? Flow->Data() : nullptr; }
USimumavClient* SSmvShell::Client() const { return Flow.IsValid() ? Flow->Client() : nullptr; }
ASmvMenuScene* SSmvShell::Scene() const { return GameMode.IsValid() ? GameMode->GetMenuScene() : nullptr; }

FString SSmvShell::SeasonId()
{
	if (FSmvUiPrefs::Get().Season == TEXT("off")) return FString();
	const FDateTime N = FDateTime::Now();
	const int32 Y = N.GetYear(), C = N.GetMonth() * 100 + N.GetDay();
	auto In = [C](int32 A, int32 B) { return C >= A && C <= B; };
	if (In(101, 101) || In(1231, 1231)) return TEXT("newyear");
	if (In(423, 423)) return TEXT("children");
	if (In(501, 501)) return TEXT("labour");
	if (In(519, 519)) return TEXT("youth");
	if (In(715, 715)) return TEXT("democracy");
	if (In(830, 830)) return TEXT("victory");
	if (In(1028, 1029)) return TEXT("republic");
	// dini bayramlar (hicri — yaklaşık; Diyanet takvimi)
	if ((Y == 2026 && In(320, 322)) || (Y == 2027 && In(309, 311)) || (Y == 2028 && In(226, 228))) return TEXT("ramadan");
	if ((Y == 2026 && In(527, 530)) || (Y == 2027 && In(516, 519)) || (Y == 2028 && In(504, 507))) return TEXT("sacrifice");
	return FString();
}

FLinearColor SSmvShell::SeasonColor(const FString& Id)
{
	if (Id == TEXT("republic") || Id == TEXT("victory") || Id == TEXT("youth") || Id == TEXT("children") || Id == TEXT("democracy")) return Hex(0xe30a17, 0.9f);
	if (Id == TEXT("ramadan") || Id == TEXT("sacrifice")) return Hex(0x34d399, 0.9f);
	if (Id == TEXT("newyear")) return Hex(0xfbbf24, 0.9f);
	if (Id == TEXT("labour")) return Hex(0xf87171, 0.9f);
	return FLinearColor::Transparent;
}

// ---------------------------------------------------------------------------
// yaşam döngüsü
// ---------------------------------------------------------------------------

void SSmvShell::Construct(const FArguments& InArgs)
{
	Flow = InArgs._Flow;
	GameMode = InArgs._GameMode;
	TextStyle = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
	TextStyle.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Hex(0x0b1222, 0.92f), 10.f, Edge(), 1.f));
	TextStyle.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Hex(0x0f1a30, 0.95f), 10.f, Hex(0x8eaae6, 0.35f), 1.f));
	TextStyle.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Hex(0x0f1a30, 0.98f), 10.f, Accent(), 1.5f));
	TextStyle.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Hex(0x0b1222, 0.6f), 10.f, Edge(), 1.f));
	TextStyle.SetPadding(FMargin(12.f, 8.f));
	TextStyle.SetForegroundColor(Fg());
	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	const FString Season = SeasonId();
	if (Data()) { EditSettings = Data()->Settings(); ReducedMotion() = EditSettings.bReducedMotion; }

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(BackdropBox, SBox).Visibility(EVisibility::HitTestInvisible)
			[
				SNew(SSmvBackdrop)
				.Opacity_Lambda([this]()
				{
					const bool bLive = Scene() && Scene()->IsLive();
					if (!bLive) return 1.f;
					return (Built == ESmvScreen::Menu || Built == ESmvScreen::Welcome) ? 0.30f : 0.80f;
				})
				.Tint(SeasonColor(Season))
			]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(Content, SBox)
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 96, 28, 0)
		[
			SNew(SBox).WidthOverride(420.f).Visibility(EVisibility::HitTestInvisible)
			[
				SAssignNew(ToastStack, SVerticalBox)
			]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(OverlayBox, SBox).Visibility_Lambda([this]() { return bOverlay ? EVisibility::Visible : EVisibility::Collapsed; })
		]
	];
	// kabuk tüm ekranı kaplar: kendisi tıklamayı yutmasın (alttaki uçuş arayüzü düğmeleri çalışsın)
	SetVisibility(EVisibility::SelfHitTestInvisible);
	Refresh();
}

void SSmvShell::Refresh() { bDirty = true; }

void SSmvShell::ShowToast(const FString& Text, int32 Severity)
{
	if (Text.TrimStartAndEnd().IsEmpty()) return;   // boş metinli bildirim gösterilmez (web toast)
	Toasts.Add({ Text, Severity, FPlatformTime::Seconds() });
	while (Toasts.Num() > 4) Toasts.RemoveAt(0);
	if (Severity <= 3) FSmvUiSound::Play(ESmvSnd::Error);
	ToastsBuilt = -1;
}

void SSmvShell::Toast(const FString& T, int32 Sev) { ShowToast(T, Sev); }

void SSmvShell::RebuildToasts()
{
	if (!ToastStack.IsValid()) return;
	ToastStack->ClearChildren();
	for (int32 i = 0; i < Toasts.Num(); ++i)
	{
		const FToast X = Toasts[i];
		const FLinearColor C = X.Sev <= 3 ? Danger() : X.Sev == 4 ? Warn() : X.Sev == 5 ? Accent() : Accent3();
		const ESmvIcon Ic = X.Sev <= 3 ? ESmvIcon::Info : X.Sev == 4 ? ESmvIcon::Bolt : X.Sev == 5 ? ESmvIcon::Info : ESmvIcon::Check;
		const double At = X.At;
		ToastStack->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SBorder).BorderImage(GlassCard()).Padding(FMargin(0.f))
			.RenderTransform_Lambda([At]()
			{
				const float K = FMath::Clamp((float)(FPlatformTime::Seconds() - At) * 5.f, 0.f, 1.f);
				return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f((1.f - K) * (1.f - K) * 80.f, 0.f)));
			})
			.ColorAndOpacity_Lambda([At]()
			{
				const double Age = FPlatformTime::Seconds() - At;
				const float A = FMath::Clamp((float)Age * 5.f, 0.f, 1.f) * FMath::Clamp((float)(5.0 - Age) / 0.4f, 0.f, 1.f);
				return FLinearColor(1, 1, 1, A);
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(4.f)[SNew(SBorder).BorderImage(Round(2.f)).BorderBackgroundColor(C)]]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 10, 10, 10)[Icon(Ic, 20.f, C)]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 10, 14, 10)[SmvUi::Text(FText::FromString(X.Text), 12, "Regular", Fg())]
			]
		];
	}
	ToastsBuilt = Toasts.Num();
}

void SSmvShell::FocusDefault()
{
	if (DefaultFocus.IsValid()) FSlateApplication::Get().SetAllUserFocus(DefaultFocus, EFocusCause::SetDirectly);
}

void SSmvShell::CloseOverlay()
{
	bOverlay = false;
	if (OverlayBox.IsValid()) OverlayBox->SetContent(SNullWidget::NullWidget);
	FSmvUiSound::Play(ESmvSnd::Back);
	const bool bFlightNoPause = Built == ESmvScreen::Flight && !(GameMode.IsValid() && GameMode->IsPausedMenu());
	if (bFlightNoPause) FSlateApplication::Get().SetAllUserFocusToGameViewport();
	else bWantFocus = true;
}

void SSmvShell::ToggleHelp()
{
	if (bOverlay) { CloseOverlay(); return; }
	if (!OverlayBox.IsValid()) return;
	const int32 Mode = Data() ? Data()->Settings().StickMode : 2;
	OverlayBox->SetContent(SmvUi::HelpOverlay([this]() { CloseOverlay(); }, Built == ESmvScreen::Flight, Mode));
	bOverlay = true;
	FSmvUiSound::Play(ESmvSnd::Open);
}

void SSmvShell::OpenTutorial()
{
	if (!OverlayBox.IsValid()) return;
	TArray<SSmvTutorial::FPage> P;
	auto Add = [&P](ESmvIcon I, const TCHAR* T, const TCHAR* B) { SSmvTutorial::FPage X; X.Icon = I; X.Title = FSmvI18n::T(T); X.Body = FSmvI18n::T(B); P.Add(X); };
	Add(ESmvIcon::Drone, TEXT("tut.t1"), TEXT("tut.b1"));
	Add(ESmvIcon::Plane, TEXT("tut.t2"), TEXT("tut.b2"));
	Add(ESmvIcon::Map, TEXT("tut.t3"), TEXT("tut.b3"));
	Add(ESmvIcon::Gamepad, TEXT("tut.t4"), TEXT("tut.b4"));
	Add(ESmvIcon::Stick, TEXT("tut.t5"), TEXT("tut.b5"));
	Add(ESmvIcon::Keyboard, TEXT("tut.t6"), TEXT("tut.b6"));
	TSharedPtr<SSmvTutorial> Tut;
	OverlayBox->SetContent(SAssignNew(Tut, SSmvTutorial).Pages(P).OnDone([this]()
	{
		FSmvUiPrefs::Get().bTutorialDone = true;
		FSmvUiPrefs::Get().Save();
		CloseOverlay();
	}));
	bOverlay = true;
	FSmvUiSound::Play(ESmvSnd::Open);
	if (Tut.IsValid() && Tut->FocusTarget().IsValid()) DefaultFocus = Tut->FocusTarget();
	bWantFocus = true;
}

void SSmvShell::Tick(const FGeometry& G, const double Now, const float Dt)
{
	SCompoundWidget::Tick(G, Now, Dt);
	// bildirimler: 5 sn sonra düşer
	const double T = FPlatformTime::Seconds();
	const int32 Before = Toasts.Num();
	Toasts.RemoveAll([T](const FToast& X) { return T - X.At > 5.0; });
	if (Toasts.Num() != Before || ToastsBuilt != Toasts.Num()) RebuildToasts();
	if (!Flow.IsValid()) return;
	const bool bPause = GameMode.IsValid() && GameMode->IsPausedMenu();
	const ESmvScreen Cur = Flow->GetScreen();
	if (BackdropBox.IsValid()) BackdropBox->SetVisibility(Cur == ESmvScreen::Flight ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	if (bDirty || Built != Cur || bBuiltPause != bPause)
	{
		const bool bScreenChanged = Built != Cur;
		bDirty = false;
		Built = Cur;
		bBuiltPause = bPause;
		// ekrana girerken düzenleme tamponlarını tazele
		if (bScreenChanged && Data())
		{
			if (Built == ESmvScreen::Settings) { EditSettings = Data()->Settings(); GSmvDisp.Load(); SetTab = 0; }
			if (Built == ESmvScreen::Hangar && Data()->Drones().Num()) EditDrone = Data()->DefaultDrone() ? *Data()->DefaultDrone() : Data()->Drones()[0];
			if (Built == ESmvScreen::Crew && Data()->Pilots().Num()) EditPilot = Data()->DefaultPilot() ? *Data()->DefaultPilot() : Data()->Pilots()[0];
			if (Built == ESmvScreen::Tracks && Data()->Tracks().Num()) EditTrack = Data()->DefaultTrack() ? *Data()->DefaultTrack() : Data()->Tracks()[0];
			if (Built == ESmvScreen::Leaderboard) Flow->FetchLeaderboard();
			if (Built == ESmvScreen::Wizard) WizStep = 0;
			FSmvUiSound::Play(ESmvSnd::Open);
		}
		DefaultFocus.Reset();
		TabStep = nullptr;
		bPreviewVisible = false;
		Content->SetContent(BuildScreen(Built));
		bWantFocus = true;
		// ilk açılış tanıtımı (ana menüye ilk girişte)
		if (Built == ESmvScreen::Menu && !FSmvUiPrefs::Get().bTutorialDone && !bOverlay) OpenTutorial();
	}
	if (ASmvMenuScene* S = Scene()) S->SetPreviewActive(bPreviewVisible);
	// odak: menü ekranlarında odak kabuk dışına (oyun görünümüne) kaçarsa varsayılana döner — gamepad/klavye gezinmesi kopmasın
	const bool bMenuScreen = Built != ESmvScreen::Flight || bPause;
	if (bMenuScreen)
	{
		if (bWantFocus && DefaultFocus.IsValid()) { FocusDefault(); bWantFocus = false; }
		else if (!HasFocusedDescendants() && DefaultFocus.IsValid()) FocusDefault();
	}
}

FReply SSmvShell::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	if (!E.GetCursorDelta().IsNearlyZero()) PadActive() = false;
	return FReply::Unhandled();
}

FNavigationReply SSmvShell::OnNavigation(const FGeometry& G, const FNavigationEvent& E)
{
	if (E.GetNavigationGenesis() == ENavigationGenesis::Controller) PadActive() = true;
	return SCompoundWidget::OnNavigation(G, E);
}

FReply SSmvShell::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	const FKey K = E.GetKey();
	if (K.IsGamepadKey()) PadActive() = true;
	const TSharedPtr<SWidget> Fw = FSlateApplication::Get().GetKeyboardFocusedWidget();
	const bool bTyping = Fw.IsValid() && (Fw->GetType() == FName(TEXT("SEditableText")) || Fw->GetType() == FName(TEXT("SMultiLineEditableText")));
	const bool bPause = GameMode.IsValid() && GameMode->IsPausedMenu();
	const EUINavigationAction Act = FSlateApplication::Get().GetNavigationActionFromKey(E);
	if (K == EKeys::F1 || (K == EKeys::Gamepad_FaceButton_Top && !bTyping)) { ToggleHelp(); return FReply::Handled(); }
	if (bOverlay)
	{
		if (Act == EUINavigationAction::Back) { if (!FSmvUiPrefs::Get().bTutorialDone) { FSmvUiPrefs::Get().bTutorialDone = true; FSmvUiPrefs::Get().Save(); } CloseOverlay(); return FReply::Handled(); }
		return FReply::Unhandled();
	}
	if (Built == ESmvScreen::Flight)
	{
		if (bPause && (Act == EUINavigationAction::Back || K == EKeys::Gamepad_Special_Right)) { if (GameMode.IsValid()) GameMode->TogglePause(); return FReply::Handled(); }
		return FReply::Unhandled();
	}
	if (!bTyping && TabStep && (K == EKeys::Gamepad_LeftShoulder || K == EKeys::Q || K == EKeys::Gamepad_RightShoulder || K == EKeys::E))
	{
		TabStep(K == EKeys::Gamepad_LeftShoulder || K == EKeys::Q ? -1 : 1);
		FSmvUiSound::Play(ESmvSnd::Toggle);
		return FReply::Handled();
	}
	if (Act == EUINavigationAction::Back && !bTyping)
	{
		if (Built == ESmvScreen::Menu || Built == ESmvScreen::Welcome || Built == ESmvScreen::Loading || Built == ESmvScreen::Summary) return FReply::Handled();
		if (Built == ESmvScreen::Wizard && WizStep > 0) { WizStep--; Refresh(); FSmvUiSound::Play(ESmvSnd::Back); return FReply::Handled(); }
		FSmvUiSound::Play(ESmvSnd::Back);
		Flow->Back();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ---------------------------------------------------------------------------
// yardımcı bileşenler
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Btn(const FText& L, TFunction<void()> OnClick, bool bPrimary, TAttribute<bool> Enabled, ESmvIcon Ic)
{
	TSharedRef<SSmvButton> B = SNew(SSmvButton).Text(L).Kind(bPrimary ? ESmvBtn::Primary : ESmvBtn::Normal).Icon(Ic).IsEnabled(Enabled).OnClick(OnClick);
	if (!DefaultFocus.IsValid()) DefaultFocus = B;
	return B;
}

TSharedRef<SWidget> SSmvShell::TextEdit(FString* Target, bool bPassword, const FText& Hint, float MinW)
{
	return SNew(SBox).MinDesiredWidth(MinW)
	[
		SNew(SEditableTextBox)
		.Style(&TextStyle)
		.Font(Font(13))
		.Text(FText::FromString(*Target))
		.HintText(Hint)
		.IsPassword(bPassword)
		.OnTextChanged_Lambda([Target](const FText& T) { *Target = T.ToString(); })
	];
}

TSharedRef<SWidget> SSmvShell::Spin(double* Target, double Min, double Max, double Step)
{
	return SNew(SBox).WidthOverride(240.f)
	[
		SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Hex(0x0b1222, 0.92f)).Padding(FMargin(6.f, 3.f))
		[
			SNew(SSpinBox<double>)
			.MinValue(Min).MaxValue(Max).Delta(Step)
			.Font(Font(12))
			.Value_Lambda([Target]() { return *Target; })
			.OnValueChanged_Lambda([Target](double V) { *Target = V; })
		]
	];
}

TSharedRef<SWidget> SSmvShell::Slider(double* Target, double Min, double Max, double Step, TFunction<FString(double)> Fmt, TFunction<void()> After)
{
	return SNew(SSmvSlider).Min(Min).Max(Max).Step(Step).Width(320.f).Format(Fmt)
		.Value_Lambda([Target]() { return *Target; })
		.OnChanged([Target, After](double V) { *Target = V; if (After) After(); });
}

TSharedRef<SWidget> SSmvShell::SliderF(float* Target, float Min, float Max, float Step, TFunction<FString(double)> Fmt, TFunction<void()> After)
{
	return SNew(SSmvSlider).Min(Min).Max(Max).Step(Step).Width(320.f).Format(Fmt)
		.Value_Lambda([Target]() { return (double)*Target; })
		.OnChanged([Target, After](double V) { *Target = (float)V; if (After) After(); });
}

TSharedRef<SWidget> SSmvShell::SliderI(int32* Target, int32 Min, int32 Max, int32 Step, TFunction<FString(double)> Fmt, TFunction<void()> After)
{
	if (!Fmt) Fmt = [](double V) { return FString::FromInt(FMath::RoundToInt(V)); };
	return SNew(SSmvSlider).Min(Min).Max(Max).Step(Step).Width(320.f).Format(Fmt)
		.Value_Lambda([Target]() { return (double)*Target; })
		.OnChanged([Target, After](double V) { *Target = FMath::RoundToInt(V); if (After) After(); });
}

TSharedRef<SWidget> SSmvShell::Toggle(bool* Target, TFunction<void()> After)
{
	return SNew(SSmvToggle).Value_Lambda([Target]() { return *Target; }).OnChanged([Target, After](bool b) { *Target = b; if (After) After(); });
}

TSharedRef<SWidget> SSmvShell::Cycle(FString* Target, const TArray<FString>& Values, const TArray<FText>& Labels, TFunction<void()> OnPick, float Width)
{
	TArray<FText> L = Labels;
	while (L.Num() < Values.Num()) L.Add(FText::FromString(Values[L.Num()]));
	return SNew(SSmvCycler).Labels(L).Width(Width)
		.Index_Lambda([Target, Values]() { const int32 I = Values.IndexOfByKey(*Target); return I < 0 ? 0 : I; })
		.OnChanged([Target, Values, OnPick](int32 I) { if (Values.IsValidIndex(I)) { *Target = Values[I]; if (OnPick) OnPick(); } });
}

TSharedRef<SWidget> SSmvShell::Chips(FString* Target, const TArray<FString>& Values, const TArray<FText>& Labels, const TArray<ESmvIcon>& Icons, TFunction<void()> OnPick, int32 PerRow)
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	TSharedPtr<SHorizontalBox> Row;
	for (int32 i = 0; i < Values.Num(); ++i)
	{
		if (i % FMath::Max(1, PerRow) == 0) { Row = SNew(SHorizontalBox); V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Row.ToSharedRef()]; }
		const FString Val = Values[i];
		Row->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(12)
			.Text(Labels.IsValidIndex(i) ? Labels[i] : FText::FromString(Val))
			.Icon(Icons.IsValidIndex(i) ? Icons[i] : ESmvIcon::None)
			.Selected_Lambda([Target, Val]() { return *Target == Val; })
			.OnClick([Target, Val, OnPick]() { *Target = Val; if (OnPick) OnPick(); })
		];
	}
	return V;
}

TSharedRef<SWidget> SSmvShell::ColorPick(FString* HexStr, TFunction<void()> After)
{
	TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
	for (const FString& C : SmvPalette())
	{
		const FLinearColor Col = SmvHex(C);
		H->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Ghost).MinHeight(36.f)
			.Selected_Lambda([HexStr, C]() { return HexStr->Equals(C, ESearchCase::IgnoreCase); })
			.OnClick([HexStr, C, After]() { *HexStr = C; if (After) After(); })
			.Extra()
			[
				SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(Col)]
			]
		];
	}
	H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)[TextEdit(HexStr, false, FText::FromString(TEXT("#rrggbb")), 110.f)];
	return H;
}

TSharedRef<SWidget> SSmvShell::ListItem(const FText& Title, const FText& Sub, ESmvIcon Ic, bool bSelected, bool bDefault, TFunction<void()> OnClick, const FLinearColor& Ac)
{
	TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Normal).LeftAlign(true).MinWidth(300.f).Accent(Ac).Selected(bSelected).OnClick(OnClick)
		.Extra()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Icon(Ic, 28.f, Ac)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(Title, 13, "Bold", Fg(), false)]
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(Sub, 10, "Regular", Dim(), false)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				bDefault ? Icon(ESmvIcon::Star, 16.f, Warn()) : SNullWidget::NullWidget
			]
		];
	if (!DefaultFocus.IsValid() || bSelected) DefaultFocus = B;
	return B;
}

TSharedRef<SWidget> SSmvShell::Tabs(const TArray<FText>& Labels, const TArray<ESmvIcon>& Icons, int32* Index)
{
	TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
	for (int32 i = 0; i < Labels.Num(); ++i)
	{
		H->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(12).Text(Labels[i]).Icon(Icons.IsValidIndex(i) ? Icons[i] : ESmvIcon::None)
			.Selected(*Index == i)
			.OnClick([this, Index, i]() { *Index = i; Refresh(); })
		];
	}
	const int32 N = Labels.Num();
	TabStep = [this, Index, N](int32 D) { *Index = (*Index + D + N) % N; Refresh(); };
	return H;
}

TSharedRef<SWidget> SSmvShell::StatBar(const FText& Label, float V, const FLinearColor& C)
{
	TSharedRef<SSmvCanvas> Bar = SNew(SSmvCanvas).Size(FVector2D(180, 8));
	const double Born = FPlatformTime::Seconds();
	Bar->Painter = [V, C, Born](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D S = FVector2D(G.GetLocalSize());
		const float K = ReducedMotion() ? 1.f : FMath::Clamp((float)(FPlatformTime::Seconds() - Born) * 2.5f, 0.f, 1.f);
		DrBox(Out, L, G, FVector2D::ZeroVector, S, FLinearColor(1, 1, 1, 0.08f), Round(4.f));
		DrBox(Out, L + 1, G, FVector2D::ZeroVector, FVector2D(S.X * V * (1.f - FMath::Pow(1.f - K, 3.f)), S.Y), C, Round(4.f));
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 3, 10, 3)[SNew(SBox).WidthOverride(110.f)[SmvUi::Text(Label, 11, "Regular", Dim(), false)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Bar];
}

TSharedRef<SWidget> SSmvShell::PreviewCard(const FString& Type, const FString& Model, const FString& ColorHex, float W, float H)
{
	ASmvMenuScene* S = Scene();
	const bool b3d = S && FSmvUiPrefs::Get().bPreview3d && S->GetPreviewTarget();
	if (b3d)
	{
		S->SetPreview(Type, Model, ColorHex);
		PreviewBrush.SetResourceObject(S->GetPreviewTarget());
		PreviewBrush.ImageSize = FVector2D(960, 600);
		bPreviewVisible = true;
	}
	const FLinearColor Col = SmvHex(ColorHex, Accent());
	const ESmvIcon Ic = IconForType(Type);
	TSharedRef<SSmvCanvas> Bg = SNew(SSmvCanvas).Size(FVector2D(W, H));
	Bg->Painter = [Col, Ic, b3d](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		const double T = SmvUi::Now();
		DrGlow(Out, L, G, Sz * 0.5, (float)Sz.Y * 0.62f, Accent().CopyWithNewOpacity(0.28f));
		// döner platform
		const FVector2D C(Sz.X * 0.5, Sz.Y * 0.80);
		for (int32 i = 0; i < 3; ++i) DrArc(Out, L + 1, G, C, (float)Sz.X * (0.18f + 0.06f * i), 0.f, 360.f, Accent().CopyWithNewOpacity(0.22f - 0.06f * i), 1.5f);
		const float A = (float)FMath::Fmod(T * 40.0, 360.0);
		DrArc(Out, L + 1, G, C, (float)Sz.X * 0.24f, A, A + 70.f, Accent().CopyWithNewOpacity(0.6f), 2.5f);
		if (!b3d)
		{
			const float Bob = 6.f * FMath::Sin((float)T * 1.6f);
			DrIcon(Out, L + 2, G, Ic, FVector2D(Sz.X * 0.5, Sz.Y * 0.45 + Bob), (float)Sz.Y * 0.5f, Col, 3.f);
		}
	};
	return SNew(SBox).WidthOverride(W).HeightOverride(H)
	[
		SNew(SBorder).BorderImage(GlassCard()).Padding(0)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[Bg]
			+ SOverlay::Slot()
			[
				b3d ? AsW<SImage>(SNew(SImage).Image(&PreviewBrush)) : SNullWidget::NullWidget
			]
		]
	];
}

TSharedRef<SWidget> SSmvShell::Portrait(const FSmvAvatar* Av, float Size)
{
	const FSmvAvatar A = Av ? *Av : FSmvAvatar();
	TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(Size, Size * 1.1f));
	C->Painter = [A](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		const float S = (float)Sz.X;
		const double T = SmvUi::Now();
		const float Br = 1.5f * FMath::Sin((float)T * 1.4f);   // nefes
		DrGlow(Out, L, G, FVector2D(S * 0.5f, S * 0.5f), S * 0.55f, Accent().CopyWithNewOpacity(0.22f));
		const FLinearColor Helmet = SmvHex(A.Helmet, Accent()), Visor = SmvHex(A.Visor, FLinearColor(0.1f, 0.1f, 0.12f)), Suit = SmvHex(A.Suit, FLinearColor(0.2f, 0.25f, 0.3f));
		const int32 SkinI = FMath::Clamp(FCString::Atoi(*A.Skin.Mid(1)) - 1, 0, 7);
		const FLinearColor Skin = SmvHex(SmvSkins()[SkinI]);
		const FLinearColor PatchC = SmvHex(A.PatchColor, Warn());
		// gövde (omuzlar)
		const float Sy = S * 0.78f + Br;
		DrBox(Out, L + 1, G, FVector2D(S * 0.14f, Sy), FVector2D(S * 0.72f, Sz.Y - Sy), Suit, Round(S * 0.18f));
		DrBox(Out, L + 2, G, FVector2D(S * 0.42f, Sy - S * 0.06f), FVector2D(S * 0.16f, S * 0.12f), Skin * 0.85f, Round(4.f));
		// arma
		if (A.Patch != TEXT("none"))
		{
			ESmvIcon Pi = ESmvIcon::Star;
			if (A.Patch == TEXT("wing")) Pi = ESmvIcon::Plane; else if (A.Patch == TEXT("bolt")) Pi = ESmvIcon::Bolt; else if (A.Patch == TEXT("chevron")) Pi = ESmvIcon::Next;
			else if (A.Patch == TEXT("target")) Pi = ESmvIcon::Stick; else if (A.Patch == TEXT("shield")) Pi = ESmvIcon::Shield; else if (A.Patch == TEXT("cross")) Pi = ESmvIcon::Plus;
			DrIcon(Out, L + 3, G, Pi, FVector2D(S * 0.66f, Sy + S * 0.14f), S * 0.13f, PatchC, 2.f);
		}
		// yüz + kask
		const FVector2D Hc(S * 0.5f, S * 0.44f + Br * 0.5f);
		const float Hr = S * 0.27f;
		DrBox(Out, L + 3, G, Hc - FVector2D(Hr * 0.78f, Hr * 0.7f), FVector2D(Hr * 1.56f, Hr * 1.6f), Skin, Round(Hr * 0.7f));
		DrBox(Out, L + 4, G, Hc - FVector2D(Hr, Hr * 1.08f), FVector2D(Hr * 2.f, Hr * 1.25f), Helmet, Round(Hr));
		DrBox(Out, L + 4, G, Hc - FVector2D(Hr * 1.04f, Hr * 0.1f), FVector2D(Hr * 0.22f, Hr * 0.9f), Helmet * 0.85f, Round(Hr * 0.1f));
		DrBox(Out, L + 4, G, Hc + FVector2D(Hr * 0.82f, -Hr * 0.1f), FVector2D(Hr * 0.22f, Hr * 0.9f), Helmet * 0.85f, Round(Hr * 0.1f));
		// vizör
		DrBox(Out, L + 5, G, Hc - FVector2D(Hr * 0.78f, Hr * 0.12f), FVector2D(Hr * 1.56f, Hr * 0.5f), Visor, Round(Hr * 0.25f));
		if (A.bMirror)
		{
			const float Sh = (float)FMath::Fmod(T * 0.4, 1.0);
			DrSeg(Out, L + 6, G, Hc + FVector2D(-Hr * 0.6f + Sh * Hr, -Hr * 0.08f), Hc + FVector2D(-Hr * 0.75f + Sh * Hr, Hr * 0.32f), FLinearColor(1, 1, 1, 0.45f), 3.f);
			DrBox(Out, L + 6, G, Hc - FVector2D(Hr * 0.78f, Hr * 0.12f), FVector2D(Hr * 1.56f, Hr * 0.14f), FLinearColor(1, 1, 1, 0.18f), Round(Hr * 0.1f));
		}
		// aksesuar
		if (A.Accessory == TEXT("mic")) DrLine(Out, L + 6, G, { Hc + FVector2D(Hr * 0.9f, Hr * 0.3f), Hc + FVector2D(Hr * 0.6f, Hr * 0.75f), Hc + FVector2D(Hr * 0.15f, Hr * 0.78f) }, FLinearColor(0.1f, 0.1f, 0.1f, 1.f), 3.f);
		if (A.Accessory == TEXT("antenna"))
		{
			DrSeg(Out, L + 6, G, Hc + FVector2D(Hr * 0.55f, -Hr * 0.9f), Hc + FVector2D(Hr * 0.8f, -Hr * 1.6f), Fg(), 2.f);
			DrBox(Out, L + 6, G, Hc + FVector2D(Hr * 0.8f - 4.f, -Hr * 1.6f - 4.f), FVector2D(8, 8), Danger(), Circle());
		}
		if (A.Accessory == TEXT("crest")) DrArc(Out, L + 6, G, Hc - FVector2D(0, Hr * 0.2f), Hr * 1.02f, 200.f, 340.f, PatchC, 5.f);
		if (A.Accessory == TEXT("stripe")) DrSeg(Out, L + 6, G, Hc + FVector2D(0, -Hr * 1.07f), Hc + FVector2D(0, -Hr * 0.2f), PatchC, 5.f);
	};
	return C;
}

TSharedRef<SWidget> SSmvShell::MapThumb(const FString& Seed, const FString& Mode, float W, float H)
{
	const uint32 Hs = GetTypeHash(Seed);
	TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(W, H)).Visibility(EVisibility::HitTestInvisible);
	const bool bFab = FSmvFabContent::IsFabWorld(Mode) || Mode.StartsWith(TEXT("fab"));
	const bool bReal = Mode == TEXT("real") || Mode == TEXT("google3d");
	C->Painter = [Hs, bFab, bReal](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		const FLinearColor Base = bFab ? Hex(0x1f2a1c) : bReal ? Hex(0x16263a) : Hex(0x122030);
		DrBox(Out, L, G, FVector2D::ZeroVector, Sz, Base, Round(12.f));
		Out.PushClip(FSlateClippingZone(G));
		const float P1 = (Hs % 97) * 0.07f, P2 = ((Hs >> 8) % 89) * 0.05f;
		const FLinearColor Line = bFab ? Hex(0x9be37a, 0.35f) : bReal ? Hex(0x7dd3fc, 0.35f) : Hex(0x34d399, 0.3f);
		for (int32 k = 0; k < 9; ++k)
		{
			TArray<FVector2D> P;
			for (int32 i = 0; i <= 24; ++i)
			{
				const float x = (float)Sz.X * i / 24.f;
				const float y = (float)Sz.Y * (0.12f + k * 0.1f) + 9.f * FMath::Sin(x * 0.035f + P1 + k * 0.7f) + 6.f * FMath::Sin(x * 0.09f + P2 * k);
				P.Add(FVector2D(x, y));
			}
			DrLine(Out, L + 1, G, P, Line.CopyWithNewOpacity(Line.A * (0.4f + 0.06f * k)), 1.2f);
		}
		// rota + ev
		const FVector2D Ctr(Sz.X * (0.35 + 0.3 * ((Hs >> 4) % 10) / 10.0), Sz.Y * 0.55);
		DrArc(Out, L + 2, G, Ctr, (float)Sz.Y * 0.22f, 0.f, 360.f, Accent().CopyWithNewOpacity(0.7f), 1.5f);
		DrBox(Out, L + 3, G, Ctr - FVector2D(4, 4), FVector2D(8, 8), Warn(), Circle());
		Out.PopClip();
	};
	return C;
}

TSharedRef<SWidget> SSmvShell::AccountChip()
{
	const bool bIn = Client() && Client()->IsLoggedIn();
	USmvGameInstance* GI = GameMode.IsValid() ? USmvGameInstance::GetSmv(GameMode.Get()) : nullptr;
	const USmvSteamSubsystem* St = GI ? GI->GetSubsystem<USmvSteamSubsystem>() : nullptr;
	const FString Name = bIn ? Client()->GetUser().DisplayName : (St ? St->GetPersonaName() : FString(TEXT("Pilot")));
	return SNew(SBorder).BorderImage(Round(20.f)).BorderBackgroundColor(Card()).Padding(FMargin(12.f, 6.f, 16.f, 6.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Icon(ESmvIcon::User, 22.f, bIn ? Accent3() : Accent())]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(Name), 12, "Bold", Fg(), false)]
			+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(bIn ? FText::FromString(TEXT("simumav.com")) : Tx(TEXT("account.guest")), 10, "Regular", Dim(), false)]
		]
	];
}

TSharedRef<SWidget> SSmvShell::HintFooter(bool bBack, bool bTabs)
{
	TArray<FSmvHint> Kb = { { TEXT("Enter"), Tx(TEXT("hint.select")) } };
	TArray<FSmvHint> Pad = { { TEXT("pad:A"), Tx(TEXT("hint.select")) } };
	if (bBack) { Kb.Add({ TEXT("Esc"), Tx(TEXT("common.back")) }); Pad.Add({ TEXT("pad:B"), Tx(TEXT("common.back")) }); }
	if (bTabs) { Kb.Add({ TEXT("Q / E"), Tx(TEXT("hint.tabs")) }); Pad.Add({ TEXT("pad:LB"), FText::GetEmpty() }); Pad.Add({ TEXT("pad:RB"), Tx(TEXT("hint.tabs")) }); }
	Kb.Add({ TEXT("F1"), Tx(TEXT("hint.help")) });
	Pad.Add({ TEXT("pad:Y"), Tx(TEXT("hint.help")) });
	return HintBar(Kb, Pad);
}

TSharedRef<SWidget> SSmvShell::Page(const FText& InTitle, const FText& Sub, ESmvIcon Ic, TSharedRef<SWidget> Body, bool bBack, bool bScroll, bool bTabs)
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	TSharedRef<SWidget> BackW = bBack
		? AsW<SSmvButton>(SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Back).Text(Tx(TEXT("common.back"))).OnClick([F]() { if (F.IsValid()) F->Back(); }))
		: SNullWidget::NullWidget;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(56, 28, 56, 14)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 20, 0)[BackW]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)[Icon(Ic, 40.f, Accent())]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Title(InTitle, 28)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[SmvUi::Text(Sub, 12, "Regular", Dim(), false)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[AccountChip()]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(56, 0, 56, 0)
		[
			bScroll ? AsW<SScrollBox>(SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[Body]) : Body
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(56, 12, 56, 20)
		[
			HintFooter(bBack, bTabs)
		];
}

void SSmvShell::SaveSettingsQuiet()
{
	if (USmvDataService* D = Data()) D->SaveSettings(FSmvDone::CreateLambda([](bool, const FString&) {}));
}

TSharedRef<SWidget> SSmvShell::BuildScreen(ESmvScreen S)
{
	switch (S)
	{
	case ESmvScreen::Welcome: return Welcome();
	case ESmvScreen::Login: return Login();
	case ESmvScreen::Menu: return Menu();
	case ESmvScreen::Hangar: return Hangar();
	case ESmvScreen::Crew: return Crew();
	case ESmvScreen::Tracks: return Tracks();
	case ESmvScreen::Missions: return Missions();
	case ESmvScreen::Wizard: return Wizard();
	case ESmvScreen::Loading: return Loading();
	case ESmvScreen::Flight: return FlightPause();
	case ESmvScreen::Summary: return Summary();
	case ESmvScreen::Lobby: return Lobby();
	case ESmvScreen::Leaderboard: return Leaderboard();
	case ESmvScreen::Logbook: return Logbook();
	case ESmvScreen::Settings: return Settings();
	case ESmvScreen::Account: return Account();
	}
	return SNullWidget::NullWidget;
}

// ---------------------------------------------------------------------------
// hoş geldin / giriş / hesap
// ---------------------------------------------------------------------------

static TSharedRef<SWidget> SmvLogo(int32 Size)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("SIMU"))).Font(Font(Size, "Black", 120)).ColorAndOpacity(Fg())
			.ShadowOffset(FVector2D(0, 3)).ShadowColorAndOpacity(Accent().CopyWithNewOpacity(0.45f))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("MAV"))).Font(Font(Size, "Black", 120)).ColorAndOpacity(Accent())
			.ShadowOffset(FVector2D(0, 3)).ShadowColorAndOpacity(Accent2().CopyWithNewOpacity(0.55f))
		];
}

TSharedRef<SWidget> SSmvShell::Welcome()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	static FString Lang;
	Lang = FSmvI18n::Language();
	const TArray<FText> LangL = { FText::FromString(TEXT("Türkçe")), FText::FromString(TEXT("English")), FText::FromString(TEXT("Deutsch")), FText::FromString(TEXT("Français")),
		FText::FromString(TEXT("Español")), FText::FromString(TEXT("Русский")), FText::FromString(TEXT("العربية")) };
	TSharedRef<SSmvButton> Guest = SNew(SSmvButton).Kind(ESmvBtn::Hero).Icon(ESmvIcon::Play).MinWidth(360.f).MinHeight(180.f)
		.Text(Tx(TEXT("welcome.guest"))).SubText(Tx(TEXT("welcome.guest_sub"))).OnClick([F]() { if (F.IsValid()) F->ContinueAsGuest(); });
	DefaultFocus = Guest;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SmvLogo(84)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 40)[SmvUi::Text(Tx(TEXT("welcome.tagline")), 16, "Light", Dim(), false)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 20, 0)[Guest]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SSmvButton).Kind(ESmvBtn::Tile).Icon(ESmvIcon::User).MinWidth(320.f).MinHeight(180.f).Accent(Accent2())
					.Text(Tx(TEXT("welcome.login"))).SubText(Tx(TEXT("welcome.login_sub"))).OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Login); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 34, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Icon(ESmvIcon::Globe, 22.f, Dim())]
				+ SHorizontalBox::Slot().AutoWidth()[Cycle(&Lang, FSmvI18n::Languages(), LangL, [Self]()
				{
					FSmvI18n::SetLanguage(Lang);
					if (Self->Data()) { Self->Data()->MutableSettings().Lang = Lang; Self->SaveSettingsQuiet(); }
					Self->Refresh();
				}, 240.f)]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 22, 0, 0)
			[
				SNew(SBox).MaxDesiredWidth(720.f)[SNew(STextBlock).Text(Tx(TEXT("welcome.note"))).Font(Font(11)).ColorAndOpacity(Mute()).AutoWrapText(true).Justification(ETextJustify::Center)]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(56, 0, 56, 20)[HintFooter(false)];
}

TSharedRef<SWidget> SSmvShell::Login()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	USmvGameInstance* GI = GameMode.IsValid() ? USmvGameInstance::GetSmv(GameMode.Get()) : nullptr;
	TSharedRef<SWidget> Email = TextEdit(&LoginEmail, false, Tx(TEXT("login.email")), 420.f);
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	B->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("login.intro")), 12, "Regular", Dim())];
	B->AddSlot().AutoHeight().Padding(0, 18, 0, 6)[SmvUi::Text(Tx(TEXT("login.email")), 11, "Bold", Dim())];
	B->AddSlot().AutoHeight()[Email];
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 6)[SmvUi::Text(Tx(TEXT("login.password")), 11, "Bold", Dim())];
	B->AddSlot().AutoHeight()[TextEdit(&LoginPassword, true, Tx(TEXT("login.password")), 420.f)];
	B->AddSlot().AutoHeight().Padding(0, 10)
	[
		SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Danger()).AutoWrapText(true)
		.Text_Lambda([F]() { return FText::FromString(F.IsValid() ? (F->bLoginBusy ? FSmvI18n::T(TEXT("login.busy")).ToString() : F->LoginError) : FString()); })
	];
	B->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Btn(Tx(TEXT("login.submit")), [F, Self]() { if (F.IsValid()) { F->Login(Self->LoginEmail, Self->LoginPassword); Self->LoginPassword.Empty(); } }, true,
			TAttribute<bool>::CreateLambda([F]() { return F.IsValid() && !F->bLoginBusy; }), ESmvIcon::Check)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Btn(Tx(TEXT("login.register")), [GI, Self]()
		{
			if (GI) if (USmvSteamSubsystem* St = GI->GetSubsystem<USmvSteamSubsystem>()) St->OpenUrl(Self->Client()->RegisterUrl());
		}, false, true, ESmvIcon::Globe)]
		+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("welcome.guest")), [F]() { if (F.IsValid()) F->ContinueAsGuest(); }, false, true, ESmvIcon::Play)]
	];
	B->AddSlot().AutoHeight().Padding(0, 16, 0, 0)[SmvUi::Text(Tx(TEXT("login.privacy")), 10, "Regular", Mute())];
	DefaultFocus = Email;
	return Page(Tx(TEXT("login.title")), Tx(TEXT("login.sub")), ESmvIcon::User, SNew(SBox).HAlign(HAlign_Center).Padding(FMargin(0, 20))[SNew(SBox).WidthOverride(620.f)[Glass(B, FMargin(32.f, 28.f))]]);
}

TSharedRef<SWidget> SSmvShell::Account()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	USimumavClient* C = Client();
	const bool bIn = C && C->IsLoggedIn();
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	B->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0)[Icon(ESmvIcon::User, 64.f, bIn ? Accent3() : Accent())]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SmvUi::Text(bIn ? FSmvI18n::Format(TEXT("account.logged_in"), { { TEXT("name"), C->GetUser().DisplayName }, { TEXT("email"), C->GetUser().Email } }) : Tx(TEXT("account.guest_info")), 14, "Regular", Fg())
		]
	];
	if (bIn && F.IsValid() && F->bOfferImport)
	{
		B->AddSlot().AutoHeight().Padding(0, 18, 0, 8)[SmvUi::Text(Tx(TEXT("account.import_offer")), 13, "Bold", Accent())];
		B->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Btn(Tx(TEXT("account.import")), [F]() { if (F.IsValid()) F->ImportGuest(); }, true, true, ESmvIcon::Save)]
			+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("account.import_skip")), [F]() { if (F.IsValid()) { F->bOfferImport = false; F->SetScreen(ESmvScreen::Menu); } })]
		];
	}
	else if (bIn && Data() && Data()->HasGuestData())
	{
		B->AddSlot().AutoHeight().Padding(0, 18, 0, 0)[Btn(Tx(TEXT("account.import")), [F]() { if (F.IsValid()) F->ImportGuest(); }, false, true, ESmvIcon::Save)];
	}
	B->AddSlot().AutoHeight().Padding(0, 18, 0, 0)[bIn ? Btn(Tx(TEXT("account.logout")), [F]() { if (F.IsValid()) F->Logout(); }, false, true, ESmvIcon::Power)
		: Btn(Tx(TEXT("welcome.login")), [F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Login); }, true, true, ESmvIcon::User)];
	B->AddSlot().AutoHeight().Padding(0, 18, 0, 0)[SmvUi::Text(Tx(TEXT("account.features")), 11, "Regular", Dim())];
	return Page(Tx(TEXT("menu.account")), Tx(TEXT("account.sub")), ESmvIcon::User, SNew(SBox).HAlign(HAlign_Left)[SNew(SBox).WidthOverride(760.f)[Glass(B, FMargin(28.f))]]);
}

// ---------------------------------------------------------------------------
// ana menü
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Menu()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	auto Go = [F](ESmvScreen S) { return [F, S]() { if (F.IsValid()) F->SetScreen(S); }; };
	const bool bSitl = USmvLocalFlightLink::SitlAvailable();
	USmvGameInstance* GI = GameMode.IsValid() ? USmvGameInstance::GetSmv(GameMode.Get()) : nullptr;
	const USmvSteamSubsystem* St = GI ? GI->GetSubsystem<USmvSteamSubsystem>() : nullptr;
	const bool bIn = Client() && Client()->IsLoggedIn();
	const FSmvPilot* Pl = Data() ? Data()->DefaultPilot() : nullptr;
	const FSmvDrone* Dr = Data() ? Data()->DefaultDrone() : nullptr;

	TSharedRef<SSmvButton> Hero = SNew(SSmvButton).Kind(ESmvBtn::Hero).Icon(ESmvIcon::Play).Accent(Hex(0xfbbf24)).MinHeight(170.f)
		.Text(Tx(TEXT("menu.fly"))).SubText(Tx(TEXT("menu.fly_sub")))
		.OnClick([F]() { if (F.IsValid()) { F->NewDraft(); F->SetScreen(ESmvScreen::Wizard); } });
	DefaultFocus = Hero;
	auto Tile = [](const FText& T, const FText& S, ESmvIcon I, uint32 Col, TFunction<void()> Fn) -> TSharedRef<SWidget>
	{
		return SNew(SSmvButton).Kind(ESmvBtn::Tile).Icon(I).Accent(Hex(Col)).MinHeight(148.f).Text(T).SubText(S).OnClick(Fn);
	};
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(7.f)).MinDesiredSlotHeight(150.f);
	Grid->AddSlot(0, 0)[Tile(Tx(TEXT("menu.hangar")), Tx(TEXT("menu.hangar_sub")), ESmvIcon::Drone, 0x38bdf8, Go(ESmvScreen::Hangar))];
	Grid->AddSlot(1, 0)[Tile(Tx(TEXT("menu.crew")), Tx(TEXT("menu.crew_sub")), ESmvIcon::Pilot, 0xf472b6, Go(ESmvScreen::Crew))];
	Grid->AddSlot(2, 0)[Tile(Tx(TEXT("menu.tracks")), Tx(TEXT("menu.tracks_sub")), ESmvIcon::Route, 0x34d399, Go(ESmvScreen::Tracks))];
	Grid->AddSlot(0, 1)[Tile(Tx(TEXT("menu.missions")), Tx(TEXT("menu.missions_sub")), ESmvIcon::Mission, 0xa78bfa, Go(ESmvScreen::Missions))];
	Grid->AddSlot(1, 1)[Tile(Tx(TEXT("menu.match")), Tx(TEXT("menu.match_sub")), ESmvIcon::Match, 0xf87171, Go(ESmvScreen::Lobby))];
	Grid->AddSlot(2, 1)[Tile(Tx(TEXT("menu.logbook")), Tx(TEXT("menu.logbook_sub")), ESmvIcon::Replay, 0xfb923c, Go(ESmvScreen::Logbook))];

	TSharedRef<SHorizontalBox> Small = SNew(SHorizontalBox);
	auto Sm = [&Small](const FText& T, ESmvIcon I, TFunction<void()> Fn, bool bDanger = false)
	{
		Small->AddSlot().AutoWidth().Padding(0, 0, 10, 0)[SNew(SSmvButton).Kind(bDanger ? ESmvBtn::Ghost : ESmvBtn::Normal).Icon(I).Text(T).OnClick(Fn)];
	};
	Sm(Tx(TEXT("menu.top10")), ESmvIcon::Trophy, Go(ESmvScreen::Leaderboard));
	Sm(Tx(TEXT("menu.settings")), ESmvIcon::Gear, Go(ESmvScreen::Settings));
	Sm(Tx(TEXT("menu.account")), ESmvIcon::User, Go(ESmvScreen::Account));
	Sm(Tx(TEXT("menu.help")), ESmvIcon::Help, [Self]() { Self->ToggleHelp(); });
	Sm(Tx(TEXT("menu.quit")), ESmvIcon::Power, [G = GameMode]() { if (G.IsValid()) UKismetSystemLibrary::QuitGame(G.Get(), nullptr, EQuitPreference::Quit, false); }, true);

	// sağ: durum paneli
	auto Status = [](bool bOk, const FText& T) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(SBox).WidthOverride(9.f).HeightOverride(9.f)[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(bOk ? Accent3() : Warn())]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(T, 11, "Regular", Dim())];
	};
	static const TCHAR* TipKeys[] = { TEXT("tip.1"), TEXT("tip.2"), TEXT("tip.3"), TEXT("tip.4"), TEXT("tip.5"), TEXT("tip.6"), TEXT("tip.7"), TEXT("tip.8") };
	const FString Season = SeasonId();
	TSharedRef<SVerticalBox> Right = SNew(SVerticalBox);
	if (!Season.IsEmpty())
	{
		Right->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
		[
			SNew(SBorder).BorderImage(Round(14.f)).BorderBackgroundColor(SeasonColor(Season).CopyWithNewOpacity(0.25f)).Padding(FMargin(16.f, 10.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Icon(ESmvIcon::Flag, 22.f, FLinearColor::White)]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FSmvI18n::T(FString(TEXT("season.")) + Season), 13, "Bold", FLinearColor::White)]
			]
		];
	}
	Right->AddSlot().AutoHeight()
	[
		Glass(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)[Portrait(Pl ? &Pl->Avatar : nullptr, 86.f)]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(Tx(TEXT("menu.pilot")), 10, "Bold", Mute(), false)]
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(Pl ? Pl->Name : FString(TEXT("Pilot"))), 18, "Bold", Fg(), false)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[SmvUi::Text(FText::FromString(Pl ? FString::Printf(TEXT("%d · %.1f h"), Pl->Flights, Pl->Hours) : FString()), 11, "Regular", Dim(), false)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Icon(IconForType(Dr ? Dr->Type : FString()), 20.f, Accent())]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SmvUi::Text(FText::FromString(Dr ? FString::Printf(TEXT("%s · %s"), *Dr->Name, *FSmvI18n::T(FString(TEXT("type.")) + Dr->Type).ToString()) : FString(TEXT("—"))), 12, "Regular", Fg(), false)
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)[Section(Tx(TEXT("menu.status")))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Status(bSitl, bSitl ? Tx(TEXT("menu.sitl_ok")) : Tx(TEXT("menu.sitl_missing")))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Status(St && St->IsSteamAvailable(), St && St->IsSteamAvailable() ? FSmvI18n::Format(TEXT("menu.steam_ok"), { { TEXT("name"), St->GetPersonaName() } }) : Tx(TEXT("menu.steam_off")))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Status(bIn, bIn ? FSmvI18n::Format(TEXT("menu.status_online"), { { TEXT("name"), Client()->GetUser().DisplayName } }) : Tx(TEXT("menu.cloud_off")))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Status(FSmvFabContent::Maps().Num() > 0, FSmvI18n::Format(TEXT("menu.maps_n"), { { TEXT("n"), FString::FromInt(FSmvFabContent::Maps().Num()) } }))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)[Section(Tx(TEXT("menu.tips")), Accent2())]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Fg()).AutoWrapText(true)
				.Text_Lambda([]() { const int32 I = (int32)(FPlatformTime::Seconds() / 9.0) % 8; return FSmvI18n::T(TipKeys[I]); })
			], FMargin(22.f))
	];
	Right->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SBorder).BorderImage(Round(14.f)).BorderBackgroundColor(Card()).Padding(FMargin(14.f, 8.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(SBox).WidthOverride(8.f).HeightOverride(8.f)[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor_Lambda([]() { return FSlateColor(Danger().CopyWithNewOpacity(0.5f + 0.5f * FMath::Sin((float)SmvUi::Now() * 4.f))); })]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SmvUi::Text(Self->Scene() && Self->Scene()->IsLive() ? Tx(TEXT("menu.live_proc")) : Tx(TEXT("menu.live_off")), 11, "Regular", Dim(), false)
			]
		]
	];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(56, 36, 56, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.62f).Padding(0, 0, 36, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SmvLogo(64)]
				+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 0, 22)[SmvUi::Text(Tx(TEXT("welcome.tagline")), 14, "Light", Dim(), false)]
				+ SVerticalBox::Slot().AutoHeight().Padding(7, 0, 7, 7)[Hero]
				+ SVerticalBox::Slot().AutoHeight()[Grid]
				+ SVerticalBox::Slot().AutoHeight().Padding(7, 14, 0, 0)[Small]
			]
			+ SHorizontalBox::Slot().FillWidth(0.38f).VAlign(VAlign_Top).Padding(0, 20, 0, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 0, 0, 14)[AccountChip()]
				+ SVerticalBox::Slot().AutoHeight()[Right]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(56, 10, 56, 20)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[HintFooter(false)]
			+ SHorizontalBox::Slot().AutoWidth()[SmvUi::Text(FText::FromString(TEXT("Simumav · UE 5.8 · ArduPilot SITL")), 10, "Regular", Mute(), false)]
		];
}

// ---------------------------------------------------------------------------
// hangar
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Hangar()
{
	USmvDataService* D = Data();
	if (!D) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvDrone& X : D->Drones())
	{
		const FSmvDrone Copy = X;
		const FSmvCatalogModel* M = USmvDataService::FindModel(X.ModelKey);
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ListItem(FText::FromString(X.Name),
			FText::FromString(FString::Printf(TEXT("%s · %s"), *FSmvI18n::T(FString(TEXT("type.")) + X.Type).ToString(), M ? *M->Label : *X.ModelKey)),
			IconForType(X.Type), X.Id == EditDrone.Id, X.bIsDefault, [Self, Copy]() { Self->EditDrone = Copy; Self->Refresh(); }, SmvHex(X.Color, Accent()))];
	}
	List->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Btn(Tx(TEXT("hangar.new")), [Self]()
	{
		FSmvDrone N; N.Name = FSmvI18n::T(TEXT("hangar.new_name")).ToString(); N.ModelKey = USmvDataService::DefaultModelFor(N.Type);
		if (const FSmvCatalogModel* M = USmvDataService::FindModel(N.ModelKey)) { N.Params = M->Defaults; N.Drive = M->Drive; if (!M->Airframe.IsEmpty()) N.Airframe = M->Airframe; }
		Self->EditDrone = N; Self->Refresh();
	}, false, true, ESmvIcon::Plus)];

	TArray<FText> TypeL;
	for (const FString& T : SmvTypes()) TypeL.Add(FSmvI18n::T(FString(TEXT("type.")) + T));
	TArray<FString> Models; TArray<FText> ModelL;
	for (const FSmvCatalogModel& M : USmvDataService::Catalog()) if (M.Type == EditDrone.Type) { Models.Add(M.Key); ModelL.Add(FText::FromString(M.Label)); }
	const FSmvCatalogModel* Cm = USmvDataService::FindModel(EditDrone.ModelKey);
	const bool bNew = EditDrone.Id.IsEmpty();

	// orta: önizleme + istatistik
	float Stats[4];
	SmvTypeStats(EditDrone.Type, Stats);
	TSharedRef<SVerticalBox> Mid = SNew(SVerticalBox);
	Mid->AddSlot().AutoHeight()[PreviewCard(EditDrone.Type, EditDrone.ModelKey, EditDrone.Color, 560.f, 340.f)];
	Mid->AddSlot().AutoHeight().Padding(4, 14, 0, 0)[Title(FText::FromString(EditDrone.Name.IsEmpty() ? FString(TEXT("—")) : EditDrone.Name), 22)];
	Mid->AddSlot().AutoHeight().Padding(4, 2, 0, 10)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *FSmvI18n::T(FString(TEXT("type.")) + EditDrone.Type).ToString(), Cm ? *Cm->Label : *EditDrone.ModelKey)), 12, "Regular", Dim(), false)];
	Mid->AddSlot().AutoHeight().Padding(4, 0)[StatBar(Tx(TEXT("stat.speed")), Stats[0], Accent())];
	Mid->AddSlot().AutoHeight().Padding(4, 0)[StatBar(Tx(TEXT("stat.range")), Stats[1], Accent3())];
	Mid->AddSlot().AutoHeight().Padding(4, 0)[StatBar(Tx(TEXT("stat.durability")), Stats[2], Warn())];
	Mid->AddSlot().AutoHeight().Padding(4, 0)[StatBar(Tx(TEXT("stat.agility")), Stats[3], Accent2())];
	if (Cm && !Cm->Desc.IsEmpty()) Mid->AddSlot().AutoHeight().Padding(4, 10, 0, 0)[SNew(SBox).WidthOverride(540.f)[SmvUi::Text(FText::FromString(Cm->Desc), 11, "Regular", Dim())]];

	// sağ: düzenleyici
	auto Pw = [](double V) { return V < 0 ? FSmvI18n::T(TEXT("power.default")).ToString() : FString::Printf(TEXT("%.0f"), V); };
	auto Pw1 = [](double V) { return V < 0 ? FSmvI18n::T(TEXT("power.default")).ToString() : FString::Printf(TEXT("%.1f"), V); };
	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("hangar.identity")))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hangar.name")), TextEdit(&EditDrone.Name, false, FText::GetEmpty(), 260.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hangar.type")), bNew ? Cycle(&EditDrone.Type, SmvTypes(), TypeL, [Self]()
	{
		Self->EditDrone.ModelKey = USmvDataService::DefaultModelFor(Self->EditDrone.Type);
		if (const FSmvCatalogModel* M = USmvDataService::FindModel(Self->EditDrone.ModelKey)) { Self->EditDrone.Params = M->Defaults; Self->EditDrone.Drive = M->Drive; if (!M->Airframe.IsEmpty()) Self->EditDrone.Airframe = M->Airframe; }
		Self->Refresh();
	}, 260.f) : SmvUi::Text(FSmvI18n::T(FString(TEXT("type.")) + EditDrone.Type), 13, "Bold", Fg(), false))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hangar.model")), Cycle(&EditDrone.ModelKey, Models, ModelL, [Self]()
	{
		if (const FSmvCatalogModel* M = USmvDataService::FindModel(Self->EditDrone.ModelKey)) { if (!M->Airframe.IsEmpty()) Self->EditDrone.Airframe = M->Airframe; Self->EditDrone.Drive = M->Drive; }
		Self->Refresh();
	}, 260.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hangar.color")), ColorPick(&EditDrone.Color, [Self]() { Self->Refresh(); }))];
	if (EditDrone.Type == TEXT("plane") || EditDrone.Type == TEXT("interceptor"))
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hangar.airframe")), Chips(&EditDrone.Airframe, { TEXT("classic"), TEXT("elevon"), TEXT("vtail") },
			{ Tx(TEXT("airframe.classic")), Tx(TEXT("airframe.elevon")), Tx(TEXT("airframe.vtail")) }, {}))];
	if (EditDrone.Type == TEXT("rover"))
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hangar.drive")), Chips(&EditDrone.Drive, { TEXT("ackermann"), TEXT("skid") }, { Tx(TEXT("drive.ackermann")), Tx(TEXT("drive.skid")) }, {}))];
	Ed->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(Tx(TEXT("hangar.power")), Warn())];
	if (EditDrone.Type == TEXT("plane") || EditDrone.Type == TEXT("vtol") || EditDrone.Type == TEXT("interceptor"))
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("power.max_speed")), Slider(&EditDrone.Power.MaxSpeedMs, -1, 150, 1, Pw))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("power.fuel_l")), Slider(&EditDrone.Power.FuelL, -1, 500, 0.5, Pw1))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("power.km_per_l")), Slider(&EditDrone.Power.FuelKmPerL, -1, 500, 0.5, Pw1))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("power.cells")), SliderI(&EditDrone.Power.ElecCells, -1, 16, 1, [](double V) { return V < 0 ? FSmvI18n::T(TEXT("power.default")).ToString() : FString::Printf(TEXT("%dS"), (int32)V); }))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("power.mah")), Slider(&EditDrone.Power.ElecMah, -1, 200000, 100, Pw))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("power.w")), Slider(&EditDrone.Power.ElecW, -1, 2000, 1, Pw))];
	Ed->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(Tx(TEXT("power.hint")), 10, "Regular", Mute())];
	Ed->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.save")), [Self]()
		{
			Self->Data()->SaveDrone(Self->EditDrone, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->Refresh(); }));
		}, true, true, ESmvIcon::Save)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.make_default")), [Self]() { Self->Data()->SetDefault(TEXT("drone"), Self->EditDrone.Id); Self->EditDrone.bIsDefault = true; Self->Refresh(); }, false,
			TAttribute<bool>(!bNew), ESmvIcon::Star)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Trash).Text(Tx(TEXT("common.delete"))).IsEnabled(!bNew).OnClick([Self]()
		{
			Self->Data()->DeleteDrone(Self->EditDrone.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); if (bOk && Self->Data()->Drones().Num()) Self->EditDrone = Self->Data()->Drones()[0]; Self->Refresh(); }));
		})]
	];
	return Page(Tx(TEXT("menu.hangar")), Tx(TEXT("menu.hangar_sub")), ESmvIcon::Drone, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[SNew(SBox).WidthOverride(320.f)[List]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[Mid]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Ed, FMargin(20.f))]);
}

// ---------------------------------------------------------------------------
// mürettebat (pilot oluşturucu)
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Crew()
{
	USmvDataService* D = Data();
	if (!D) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvPilot& X : D->Pilots())
	{
		const FSmvPilot Copy = X;
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ListItem(FText::FromString(X.Name), FText::FromString(FString::Printf(TEXT("%d · %.1f h"), X.Flights, X.Hours)),
			ESmvIcon::Pilot, X.Id == EditPilot.Id, X.bIsDefault, [Self, Copy]() { Self->EditPilot = Copy; Self->Refresh(); }, SmvHex(X.Avatar.Helmet, Accent()))];
	}
	List->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Btn(Tx(TEXT("crew.new")), [Self]() { FSmvPilot P; P.Name = FSmvI18n::T(TEXT("crew.new_name")).ToString(); Self->EditPilot = P; Self->Refresh(); }, false, true, ESmvIcon::Plus)];
	TArray<FString> Chars; TArray<FText> CharL;
	for (int32 i = 1; i <= 10; ++i) { Chars.Add(FString::Printf(TEXT("p%02d"), i)); CharL.Add(FText::FromString(FString::Printf(TEXT("P%02d"), i))); }
	TArray<FString> Skins; TArray<FText> SkinL;
	for (int32 i = 1; i <= 8; ++i) { Skins.Add(FString::Printf(TEXT("s%d"), i)); SkinL.Add(FText::FromString(FString::Printf(TEXT("%d"), i))); }
	const TArray<FString> Patches = { TEXT("none"), TEXT("star"), TEXT("wing"), TEXT("bolt"), TEXT("chevron"), TEXT("target"), TEXT("shield"), TEXT("cross") };
	const TArray<FString> Acc = { TEXT("none"), TEXT("mic"), TEXT("crest"), TEXT("antenna"), TEXT("stripe") };
	TArray<FText> PatchL, AccL;
	for (const FString& P : Patches) PatchL.Add(FSmvI18n::T(FString(TEXT("avatar.patch_")) + P));
	for (const FString& A : Acc) AccL.Add(FSmvI18n::T(FString(TEXT("avatar.acc_")) + A));
	const bool bNew = EditPilot.Id.IsEmpty();
	auto Re = [Self]() { Self->Refresh(); };

	TSharedRef<SVerticalBox> Mid = SNew(SVerticalBox);
	Mid->AddSlot().AutoHeight()
	[
		SNew(SBox).WidthOverride(380.f).HeightOverride(420.f)
		[
			SNew(SBorder).BorderImage(GlassCard()).HAlign(HAlign_Center).VAlign(VAlign_Center)[Portrait(&EditPilot.Avatar, 300.f)]
		]
	];
	Mid->AddSlot().AutoHeight().Padding(4, 14, 0, 0)[Title(FText::FromString(EditPilot.Name.IsEmpty() ? FString(TEXT("—")) : EditPilot.Name), 22)];
	if (!bNew) Mid->AddSlot().AutoHeight().Padding(4, 4, 0, 0)[SmvUi::Text(FSmvI18n::Format(TEXT("crew.stats"), { { TEXT("n"), FString::FromInt(EditPilot.Flights) }, { TEXT("h"), FString::Printf(TEXT("%.1f"), EditPilot.Hours) } }), 12, "Regular", Dim(), false)];
	Mid->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Btn(Tx(TEXT("crew.random")), [Self]()
	{
		FSmvAvatar& A = Self->EditPilot.Avatar;
		const TArray<FString>& Pal = SmvPalette();
		A.Helmet = Pal[FMath::RandRange(0, Pal.Num() - 1)]; A.Suit = Pal[FMath::RandRange(0, Pal.Num() - 1)]; A.Visor = Pal[FMath::RandRange(0, Pal.Num() - 1)];
		A.PatchColor = Pal[FMath::RandRange(0, Pal.Num() - 1)]; A.bMirror = FMath::RandBool();
		A.Skin = FString::Printf(TEXT("s%d"), FMath::RandRange(1, 8));
		static const TCHAR* Pt[] = { TEXT("none"), TEXT("star"), TEXT("wing"), TEXT("bolt"), TEXT("chevron"), TEXT("target"), TEXT("shield"), TEXT("cross") };
		static const TCHAR* Ac[] = { TEXT("none"), TEXT("mic"), TEXT("crest"), TEXT("antenna"), TEXT("stripe") };
		A.Patch = Pt[FMath::RandRange(0, 7)]; A.Accessory = Ac[FMath::RandRange(0, 4)];
		Self->Refresh();
	}, false, true, ESmvIcon::Star)];

	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("crew.identity")))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("crew.name")), TextEdit(&EditPilot.Name, false, FText::GetEmpty(), 260.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("crew.character")), Cycle(&EditPilot.Character, Chars, CharL, nullptr, 200.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(Tx(TEXT("crew.avatar")), Hex(0xf472b6))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.helmet")), ColorPick(&EditPilot.Avatar.Helmet, Re))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.visor")), ColorPick(&EditPilot.Avatar.Visor, Re))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.mirror")), Toggle(&EditPilot.Avatar.bMirror, Re))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.suit")), ColorPick(&EditPilot.Avatar.Suit, Re))];
	{
		TSharedRef<SHorizontalBox> Sk = SNew(SHorizontalBox);
		for (int32 i = 0; i < 8; ++i)
		{
			const FString V = Skins[i];
			Sk->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SSmvButton).Kind(ESmvBtn::Ghost).MinHeight(36.f).Selected_Lambda([Self, V]() { return Self->EditPilot.Avatar.Skin == V; })
				.OnClick([Self, V]() { Self->EditPilot.Avatar.Skin = V; Self->Refresh(); })
				.Extra()[SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(SmvHex(SmvSkins()[i]))]]
			];
		}
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.skin")), Sk)];
	}
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.patch")), Cycle(&EditPilot.Avatar.Patch, Patches, PatchL, Re, 240.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.patch_color")), ColorPick(&EditPilot.Avatar.PatchColor, Re))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.accessory")), Cycle(&EditPilot.Avatar.Accessory, Acc, AccL, Re, 240.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(Tx(TEXT("crew.avatar_note")), 10, "Regular", Mute())];
	Ed->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.save")), [Self]()
		{
			Self->Data()->SavePilot(Self->EditPilot, FSmvDone::CreateLambda([Self](bool bOk, const FString& M)
			{
				Self->Toast(bOk ? FSmvI18n::Format(TEXT("crew.welcome"), { { TEXT("name"), Self->EditPilot.Name } }).ToString() : M, bOk ? 6 : 3);
				Self->Refresh();
			}));
		}, true, true, ESmvIcon::Save)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.make_default")), [Self]() { Self->Data()->SetDefault(TEXT("pilot"), Self->EditPilot.Id); Self->EditPilot.bIsDefault = true; Self->Refresh(); }, false, TAttribute<bool>(!bNew), ESmvIcon::Star)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Trash).Text(Tx(TEXT("common.delete"))).IsEnabled(!bNew).OnClick([Self]()
		{
			Self->Data()->DeletePilot(Self->EditPilot.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); if (bOk && Self->Data()->Pilots().Num()) Self->EditPilot = Self->Data()->Pilots()[0]; Self->Refresh(); }));
		})]
	];
	return Page(Tx(TEXT("menu.crew")), Tx(TEXT("menu.crew_sub")), ESmvIcon::Pilot, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[SNew(SBox).WidthOverride(300.f)[List]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[Mid]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Ed, FMargin(20.f))]);
}

// ---------------------------------------------------------------------------
// parkurlar
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Tracks()
{
	USmvDataService* D = Data();
	if (!D) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvTrack& X : D->Tracks())
	{
		const FSmvTrack Copy = X;
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ListItem(FText::FromString(X.Name),
			FText::FromString(FString::Printf(TEXT("%s · %d"), *FSmvI18n::T(FString(TEXT("world.")) + X.WorldMode).ToString(), X.Gates.Num())),
			ESmvIcon::Route, X.Id == EditTrack.Id, X.bIsDefault, [Self, Copy]() { Self->EditTrack = Copy; Self->Refresh(); }, Accent3())];
	}
	List->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Btn(Tx(TEXT("tracks.new")), [Self]() { FSmvTrack T; T.Name = FSmvI18n::T(TEXT("tracks.new_name")).ToString(); Self->EditTrack = T; Self->Refresh(); }, false, true, ESmvIcon::Plus)];
	const bool bNew = EditTrack.Id.IsEmpty();
	auto Re = [Self]() { Self->Refresh(); };
	auto Mfmt = [](double V) { return FString::Printf(TEXT("%.0f m"), V); };
	auto Ms = [](double V) { return FString::Printf(TEXT("%.1f m/s"), V); };
	auto Deg = [](double V) { return FString::Printf(TEXT("%.0f°"), V); };
	auto Pct = [](double V) { return FString::Printf(TEXT("%.0f%%"), V * 100.0); };
	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("tracks.location")), Accent3())];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.name")), TextEdit(&EditTrack.Name, false, FText::GetEmpty(), 260.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.lat")), Spin(&EditTrack.Lat, -85, 85, 0.0001))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.lon")), Spin(&EditTrack.Lon, -180, 180, 0.0001))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.ground")), Spin(&EditTrack.GroundMsl, -10000, 8000, 1))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.radius")), Slider(&EditTrack.RadiusM, 200, 5000, 50, Mfmt))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.world")), Chips(&EditTrack.WorldMode, { TEXT("procedural"), TEXT("real"), TEXT("google3d") },
		{ Tx(TEXT("world.procedural")), Tx(TEXT("world.real")), Tx(TEXT("world.google3d")) }, { ESmvIcon::Map, ESmvIcon::Globe, ESmvIcon::Globe }, Re))];
	Ed->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(Tx(TEXT("tracks.env")), Warn())];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.time")), Chips(&EditTrack.TimeOfDay, { TEXT("day"), TEXT("dusk"), TEXT("night") },
		{ Tx(TEXT("time.day")), Tx(TEXT("time.dusk")), Tx(TEXT("time.night")) }, { ESmvIcon::Sun, ESmvIcon::Sunset, ESmvIcon::Moon }))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.weather")), Chips(&EditTrack.Weather, { TEXT("clear"), TEXT("cloudy"), TEXT("rain"), TEXT("fog"), TEXT("snow") },
		{ Tx(TEXT("weather.clear")), Tx(TEXT("weather.cloudy")), Tx(TEXT("weather.rain")), Tx(TEXT("weather.fog")), Tx(TEXT("weather.snow")) },
		{ ESmvIcon::Sun, ESmvIcon::Cloud, ESmvIcon::Rain, ESmvIcon::Fog, ESmvIcon::Snow }, nullptr, 3))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.wind")), Slider(&EditTrack.WindMs, 0, 30, 0.5, Ms))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.wind_dir")), Slider(&EditTrack.WindDir, 0, 355, 5, Deg))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.gust")), Slider(&EditTrack.WindGust, 0, 20, 0.5, Ms))];
	if (EditTrack.WorldMode == TEXT("procedural"))
	{
		Ed->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(Tx(TEXT("tracks.proc")), Accent2())];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.seed")), SliderI(&EditTrack.World.Seed, 1, 999999, 1))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.size")), Slider(&EditTrack.World.SizeM, 1000, 12000, 500, Mfmt))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.mountains")), SliderI(&EditTrack.World.MountainCount, 0, 12))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.mountain_h")), Slider(&EditTrack.World.MountainHeight, 0, 1500, 10, Mfmt))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.trees")), Slider(&EditTrack.World.TreeDensity, 0, 1, 0.05, Pct))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.buildings")), SliderI(&EditTrack.World.BuildingCount, 0, 400, 5))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.runway")), Toggle(&EditTrack.World.bRunway))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.runway_hdg")), Slider(&EditTrack.World.RunwayHeading, 0, 355, 5, Deg))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.water")), Slider(&EditTrack.World.Water, 0, 1, 0.05, Pct))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.fog")), Slider(&EditTrack.World.Fog, 0, 1, 0.05, Pct))];
	}
	Ed->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(FSmvI18n::Format(TEXT("tracks.gates_n"), { { TEXT("n"), FString::FromInt(EditTrack.Gates.Num()) } }), Hex(0xf472b6))];
	Ed->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("tracks.gates_ring")), [Self]()
		{
			FSmvTrack& T = Self->EditTrack;
			T.Gates.Reset();
			for (int32 i = 0; i < 8; ++i)
			{
				const double A = 2.0 * UE_DOUBLE_PI * i / 8, R = FMath::Min(T.RadiusM * 0.4, 400.0);
				FSmvGate G; G.I = i;
				G.Lat = T.Lat + FMath::RadiansToDegrees(R * FMath::Cos(A) / 6378137.0);
				G.Lon = T.Lon + FMath::RadiansToDegrees(R * FMath::Sin(A) / (6378137.0 * FMath::Cos(FMath::DegreesToRadians(T.Lat))));
				G.AglM = 15 + 5 * (i % 3); G.YawDeg = FMath::Fmod(FMath::RadiansToDegrees(A) + 90.0, 360.0); G.RadiusM = 6;
				T.Gates.Add(G);
			}
			Self->Refresh();
		}, false, true, ESmvIcon::Plus)]
		+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("tracks.gates_clear")), [Self]() { Self->EditTrack.Gates.Reset(); Self->Refresh(); }, false, true, ESmvIcon::Trash)]
	];
	Ed->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.save")), [Self]()
		{
			Self->Data()->SaveTrack(Self->EditTrack, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->Refresh(); }));
		}, true, true, ESmvIcon::Save)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.make_default")), [Self]() { Self->Data()->SetDefault(TEXT("track"), Self->EditTrack.Id); Self->EditTrack.bIsDefault = true; Self->Refresh(); }, false, TAttribute<bool>(!bNew), ESmvIcon::Star)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Trash).Text(Tx(TEXT("common.delete"))).IsEnabled(!bNew).OnClick([Self]()
		{
			Self->Data()->DeleteTrack(Self->EditTrack.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); if (bOk && Self->Data()->Tracks().Num()) Self->EditTrack = Self->Data()->Tracks()[0]; Self->Refresh(); }));
		})]
	];
	TSharedRef<SVerticalBox> Mid = SNew(SVerticalBox);
	Mid->AddSlot().AutoHeight()[SNew(SBox).WidthOverride(460.f).HeightOverride(280.f)[MapThumb(EditTrack.Name + EditTrack.Id, EditTrack.WorldMode, 460.f, 280.f)]];
	Mid->AddSlot().AutoHeight().Padding(4, 12, 0, 0)[Title(FText::FromString(EditTrack.Name.IsEmpty() ? FString(TEXT("—")) : EditTrack.Name), 20)];
	Mid->AddSlot().AutoHeight().Padding(4, 4, 0, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%.5f, %.5f · %.0f m · %s · %s"), EditTrack.Lat, EditTrack.Lon, EditTrack.RadiusM,
		*FSmvI18n::T(FString(TEXT("time.")) + EditTrack.TimeOfDay).ToString(), *FSmvI18n::T(FString(TEXT("weather.")) + EditTrack.Weather).ToString())), 11, "Regular", Dim(), false)];
	return Page(Tx(TEXT("menu.tracks")), Tx(TEXT("menu.tracks_sub")), ESmvIcon::Route, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[SNew(SBox).WidthOverride(300.f)[List]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[Mid]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Ed, FMargin(20.f))]);
}

// ---------------------------------------------------------------------------
// görevler
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Missions()
{
	USmvDataService* D = Data();
	if (!D) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	const TArray<FString> Kinds = { TEXT("free"), TEXT("corridor"), TEXT("spray"), TEXT("survey"), TEXT("fpv_flags"), TEXT("fpv_gates"), TEXT("defend") };
	const TArray<ESmvIcon> KindI = { ESmvIcon::Route, ESmvIcon::Map, ESmvIcon::Rain, ESmvIcon::Camera, ESmvIcon::Flag, ESmvIcon::Fpv, ESmvIcon::Shield };
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvMission& X : D->Missions())
	{
		const FSmvMission Copy = X;
		const int32 Ki = Kinds.IndexOfByKey(X.Kind);
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ListItem(FText::FromString(X.Name), FSmvI18n::T(FString(TEXT("mission.")) + X.Kind),
			KindI.IsValidIndex(Ki) ? KindI[Ki] : ESmvIcon::Mission, X.Id == EditMission.Id, false, [Self, Copy]() { Self->EditMission = Copy; Self->Refresh(); }, Accent2())];
	}
	List->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Btn(Tx(TEXT("missions.new")), [Self]()
	{
		FSmvMission M; M.Name = FSmvI18n::T(TEXT("missions.new_name")).ToString();
		if (const FSmvTrack* T = Self->Data()->DefaultTrack()) M.TrackId = T->Id;
		Self->EditMission = M; Self->Refresh();
	}, false, true, ESmvIcon::Plus)];
	TArray<FString> TrackIds; TArray<FText> TrackL;
	for (const FSmvTrack& T : D->Tracks()) { TrackIds.Add(T.Id); TrackL.Add(FText::FromString(T.Name)); }
	TArray<FText> KindL; for (const FString& K : Kinds) KindL.Add(FSmvI18n::T(FString(TEXT("mission.")) + K));
	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("missions.design")), Accent2())];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.name")), TextEdit(&EditMission.Name, false, FText::GetEmpty(), 260.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.track")), Cycle(&EditMission.TrackId, TrackIds, TrackL, nullptr, 280.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.kind")), Chips(&EditMission.Kind, Kinds, KindL, KindI, nullptr, 4))];
	Ed->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[SmvUi::Text(Tx(TEXT("missions.plan")), 12, "Bold", Fg())];
	Ed->AddSlot().AutoHeight()
	[
		SNew(SBox).HeightOverride(240)
		[
			SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Hex(0x0b1222, 0.92f)).Padding(6)
			[
				SNew(SMultiLineEditableTextBox).Text(FText::FromString(EditMission.PlanJson)).Font(Font(11, "Mono"))
				.OnTextChanged_Lambda([Self](const FText& T) { Self->EditMission.PlanJson = T.ToString(); })
			]
		]
	];
	Ed->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(Tx(TEXT("missions.plan_hint")), 10, "Regular", Mute())];
	Ed->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.save")), [Self]()
		{
			Self->Data()->SaveMission(Self->EditMission, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->Refresh(); }));
		}, true, true, ESmvIcon::Save)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Trash).Text(Tx(TEXT("common.delete"))).IsEnabled(!EditMission.Id.IsEmpty()).OnClick([Self]()
		{
			Self->Data()->DeleteMission(Self->EditMission.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); Self->EditMission = FSmvMission(); Self->Refresh(); }));
		})]
	];
	return Page(Tx(TEXT("menu.missions")), Tx(TEXT("menu.missions_sub")), ESmvIcon::Mission, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[SNew(SBox).WidthOverride(320.f)[List]]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Ed, FMargin(20.f))]);
}

// ---------------------------------------------------------------------------
// uç sihirbazı (adımlı)
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Wizard()
{
	if (!Data() || !Flow.IsValid()) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	FSmvFlightRequest& R = Flow->Draft;
	WizDroneId = R.Drone.Id; WizPilotId = R.Pilot.Id; WizTrackId = R.Track.Id; WizMissionId = R.bHasMission ? R.Mission.Id : FString(TEXT("-"));
	WizBackend = R.Backend == ESmvFlightBackend::Cloud ? TEXT("cloud") : TEXT("local");
	WizSpeed = FString::SanitizeFloat(R.Speedup);
	WizStickMode = FString::FromInt(Data()->Settings().StickMode);
	WizDevice = Data()->Settings().InputDevice;
	static const TCHAR* StepKeys[] = { TEXT("wiz.s_vehicle"), TEXT("wiz.s_map"), TEXT("wiz.s_env"), TEXT("wiz.s_control"), TEXT("wiz.s_rules"), TEXT("wiz.s_adv"), TEXT("wiz.s_go") };
	static const ESmvIcon StepIcons[] = { ESmvIcon::Drone, ESmvIcon::Map, ESmvIcon::Sun, ESmvIcon::Gamepad, ESmvIcon::Flag, ESmvIcon::Gear, ESmvIcon::Play };
	const int32 N = 7;
	WizStep = FMath::Clamp(WizStep, 0, N - 1);
	TabStep = [Self, N](int32 D) { Self->WizStep = FMath::Clamp(Self->WizStep + D, 0, N - 1); Self->Refresh(); };
	// adım çubuğu
	TSharedRef<SHorizontalBox> Stepper = SNew(SHorizontalBox);
	for (int32 i = 0; i < N; ++i)
	{
		const bool bDone = i < WizStep, bCur = i == WizStep;
		Stepper->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(12).Icon(bDone ? ESmvIcon::Check : StepIcons[i]).Selected(bCur)
			.Accent(bDone ? Accent3() : Accent())
			.Text(FText::FromString(FString::Printf(TEXT("%d  %s"), i + 1, *FSmvI18n::T(StepKeys[i]).ToString())))
			.OnClick([Self, i]() { Self->WizStep = i; Self->Refresh(); })
		];
		if (i < N - 1) Stepper->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[SNew(SBox).WidthOverride(14.f).HeightOverride(2.f)[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(bDone ? Accent3() : Edge())]];
	}
	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	switch (WizStep)
	{
	case 0: Body = WizVehicle(); break;
	case 1: Body = WizMap(); break;
	case 2: Body = WizEnv(); break;
	case 3: Body = WizControl(); break;
	case 4: Body = WizRules(); break;
	case 5: Body = WizAdvanced(); break;
	default: Body = WizSummary(); break;
	}
	TSharedRef<SSmvButton> Next = SNew(SSmvButton).Kind(WizStep == N - 1 ? ESmvBtn::Hero : ESmvBtn::Primary).MinWidth(200.f)
		.Icon(WizStep == N - 1 ? ESmvIcon::Play : ESmvIcon::Next)
		.Text(WizStep == N - 1 ? Tx(TEXT("wizard.start")) : Tx(TEXT("tut.next")))
		.Accent(WizStep == N - 1 ? Hex(0xfbbf24) : Accent())
		.OnClick([Self, N]() { if (Self->WizStep >= N - 1) Self->StartFromWizard(); else { Self->WizStep++; Self->Refresh(); } });
	if (!DefaultFocus.IsValid()) DefaultFocus = Next;
	TSharedRef<SWidget> Footer = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Icon(ESmvIcon::Prev).Text(Tx(TEXT("tut.prev"))).IsEnabled(WizStep > 0).OnClick([Self]() { Self->WizStep = FMath::Max(0, Self->WizStep - 1); Self->Refresh(); })]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).HAlign(HAlign_Center)
		[
			SmvUi::Text(FText::FromString(Self->Flow->ValidateDraft()), 12, "Bold", Warn(), false)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
		[
			WizStep < N - 1 ? AsW<SSmvButton>(SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Play).Text(Tx(TEXT("wiz.quick"))).OnClick([Self]() { Self->StartFromWizard(); })) : SNullWidget::NullWidget
		]
		+ SHorizontalBox::Slot().AutoWidth()[Next];
	return Page(Tx(TEXT("wizard.title")), Tx(TEXT("wizard.sub")), ESmvIcon::Play,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)[Stepper]
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[Body]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)[Footer], true, false, true);
}

TSharedRef<SWidget> SSmvShell::WizVehicle()
{
	SSmvShell* Self = this;
	USmvFlow* F = Flow.Get();
	FSmvFlightRequest& R = F->Draft;
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(6.f)).MinDesiredSlotWidth(220.f).MinDesiredSlotHeight(120.f);
	int32 i = 0;
	for (const FSmvDrone& X : Data()->Drones())
	{
		const FSmvDrone Copy = X;
		const FSmvCatalogModel* M = USmvDataService::FindModel(X.ModelKey);
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Icon(IconForType(X.Type)).Accent(SmvHex(X.Color, Accent()))
			.Selected(X.Id == R.Drone.Id).Text(FText::FromString(X.Name))
			.SubText(FText::FromString(FString::Printf(TEXT("%s · %s"), *FSmvI18n::T(FString(TEXT("type.")) + X.Type).ToString(), M ? *M->Label : *X.ModelKey)))
			.OnClick([Self, F, Copy]()
			{
				F->Draft.Drone = Copy;
				F->Draft.Launch = FSmvLaunch();
				if (Copy.Type == TEXT("plane")) F->Draft.Launch.Method = TEXT("wheels");
				Self->Refresh();
			});
		if (X.Id == R.Drone.Id) DefaultFocus = B;
		Grid->AddSlot(i % 3, i / 3)[B];
		++i;
	}
	TArray<FString> Ids; TArray<FText> Ls;
	for (const FSmvPilot& X : Data()->Pilots()) { Ids.Add(X.Id); Ls.Add(FText::FromString(X.Name)); }
	float Stats[4];
	SmvTypeStats(R.Drone.Type, Stats);
	TSharedRef<SVerticalBox> Right = SNew(SVerticalBox);
	Right->AddSlot().AutoHeight()[PreviewCard(R.Drone.Type, R.Drone.ModelKey, R.Drone.Color, 520.f, 320.f)];
	Right->AddSlot().AutoHeight().Padding(4, 12, 0, 6)[Title(FText::FromString(R.Drone.Name), 20)];
	Right->AddSlot().AutoHeight().Padding(4, 0)[StatBar(Tx(TEXT("stat.speed")), Stats[0], Accent())];
	Right->AddSlot().AutoHeight().Padding(4, 0)[StatBar(Tx(TEXT("stat.range")), Stats[1], Accent3())];
	Right->AddSlot().AutoHeight().Padding(4, 0)[StatBar(Tx(TEXT("stat.durability")), Stats[2], Warn())];
	Right->AddSlot().AutoHeight().Padding(4, 0)[StatBar(Tx(TEXT("stat.agility")), Stats[3], Accent2())];
	Right->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[Row(Tx(TEXT("wizard.pilot")), Cycle(&WizPilotId, Ids, Ls, [Self, F]()
	{
		for (const FSmvPilot& X : Self->Data()->Pilots()) if (X.Id == Self->WizPilotId) F->Draft.Pilot = X;
	}, 240.f))];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 22, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wizard.vehicle")))]
			+ SVerticalBox::Slot().AutoHeight()[Grid]
			+ SVerticalBox::Slot().AutoHeight().Padding(6, 8, 0, 0)[Btn(Tx(TEXT("wiz.to_hangar")), [F]() { F->SetScreen(ESmvScreen::Hangar); }, false, true, ESmvIcon::Drone)]
		]
		+ SHorizontalBox::Slot().AutoWidth()[Right];
}

TSharedRef<SWidget> SSmvShell::WizMap()
{
	SSmvShell* Self = this;
	USmvFlow* F = Flow.Get();
	FSmvFlightRequest& R = F->Draft;
	auto MapTile = [Self](const FText& Name, const FText& Desc, const FString& Seed, const FString& Mode, bool bSel, TFunction<void()> Pick,
		const FSlateBrush* Img = nullptr, bool bAvail = true) -> TSharedRef<SWidget>
	{
		TSharedRef<SWidget> Pic = Img
			? AsW<SBorder>(SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(FLinearColor::White).Padding(0)[SNew(SImage).Image(Img)])
			: Self->MapThumb(Seed, Mode, 250.f, 120.f);
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Selected(bSel).Accent(Accent3()).Text(Name).MinHeight(230.f).OnClick(Pick).IsEnabled(bAvail)
			.SubText(bAvail ? Desc : FText::FromString(Desc.ToString() + TEXT("  ·  ") + FSmvI18n::T(TEXT("wiz.map_missing")).ToString()))
			.Extra()[SNew(SBox).WidthOverride(250.f).HeightOverride(120.f)[Pic]];
		if (bSel) Self->DefaultFocus = B;
		return B;
	};
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// hazır haritalar (Fab kayıt tablosu)
	const TArray<FSmvFabMap>& Maps = FSmvFabContent::AllMaps();
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wiz.maps_ready")), Accent3())];
	if (Maps.Num() == 0)
	{
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 12)[SmvUi::Text(Tx(TEXT("wiz.maps_none")), 12, "Regular", Mute())];
	}
	else
	{
		TSharedRef<SUniformGridPanel> G = SNew(SUniformGridPanel).SlotPadding(FMargin(6.f)).MinDesiredSlotWidth(270.f);
		for (int32 i = 0; i < Maps.Num(); ++i)
		{
			const FSmvFabMap M = Maps[i];
			const FString Cat = SmvMapCategory(M.Key);
			const FText Desc = !M.Description.IsEmpty() ? M.Description : FSmvI18n::T(FString(TEXT("fabdesc.")) + Cat);
			const FString Id = FString(TEXT("fab:")) + M.Key;
			G->AddSlot(i % 3, i / 3)[MapTile(M.Name, Desc, M.Key, Id, R.Track.Id == Id, [Self, F, M]()
			{
				F->Draft.Track = FSmvFabContent::MakeTrack(M);
				F->Draft.bHasMission = false;
				Self->Refresh();
			}, SmvMapPreview(M), M.bAvailable)];
		}
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 14)[G];
	}
	// parkurlarım
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wiz.maps_mine")), Accent())];
	TSharedRef<SUniformGridPanel> G2 = SNew(SUniformGridPanel).SlotPadding(FMargin(6.f)).MinDesiredSlotWidth(270.f);
	int32 i = 0;
	for (const FSmvTrack& X : Data()->Tracks())
	{
		const FSmvTrack Copy = X;
		const FText Desc = FText::FromString(FString::Printf(TEXT("%s · %s · %s · %d"), *FSmvI18n::T(FString(TEXT("world.")) + X.WorldMode).ToString(),
			*FSmvI18n::T(FString(TEXT("time.")) + X.TimeOfDay).ToString(), *FSmvI18n::T(FString(TEXT("weather.")) + X.Weather).ToString(), X.Gates.Num()));
		G2->AddSlot(i % 3, i / 3)[MapTile(FText::FromString(X.Name), Desc, X.Name + X.Id, X.WorldMode, R.Track.Id == X.Id, [Self, F, Copy]()
		{
			F->Draft.Track = Copy;
			F->Draft.bHasMission = false;
			Self->Refresh();
		})];
		++i;
	}
	V->AddSlot().AutoHeight()[G2];
	V->AddSlot().AutoHeight().Padding(6, 10, 0, 0)[Btn(Tx(TEXT("wiz.to_tracks")), [F]() { F->SetScreen(ESmvScreen::Tracks); }, false, true, ESmvIcon::Route)];
	return V;
}

TSharedRef<SWidget> SSmvShell::WizEnv()
{
	SSmvShell* Self = this;
	FSmvTrack& T = Flow->Draft.Track;
	auto Ms = [](double V) { return FString::Printf(TEXT("%.1f m/s"), V); };
	auto Deg = [](double V) { return FString::Printf(TEXT("%.0f°"), V); };
	TSharedRef<SSmvCanvas> Compass = SNew(SSmvCanvas).Size(FVector2D(200, 200));
	Compass->Painter = [Self](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		if (!Self->Flow.IsValid()) return;
		const FSmvTrack& Tr = Self->Flow->Draft.Track;
		const FVector2D C = FVector2D(G.GetLocalSize()) * 0.5;
		const float R = (float)C.X - 14.f;
		DrBox(Out, L, G, C - FVector2D(R, R), FVector2D(2 * R, 2 * R), Hex(0x0b1222, 0.85f), Circle());
		DrArc(Out, L + 1, G, C, R, 0.f, 360.f, Accent().CopyWithNewOpacity(0.6f), 1.5f);
		static const TCHAR* Card[] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };
		for (int32 k = 0; k < 4; ++k)
		{
			const float A = FMath::DegreesToRadians(-90.f + k * 90.f);
			DrText(Out, L + 1, G, Card[k], C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (R - 14.f), Font(11, "Bold"), k == 0 ? Warn() : Dim(), 0);
		}
		// rüzgâr nereden eser → ok rüzgârın gittiği yön
		const float From = FMath::DegreesToRadians((float)Tr.WindDir - 90.f);
		const FVector2D U(FMath::Cos(From), FMath::Sin(From));
		const float Len = R * FMath::Clamp(0.3f + (float)Tr.WindMs / 30.f * 0.6f, 0.3f, 0.9f);
		const FVector2D A0 = C + U * Len, A1 = C - U * Len;
		DrSeg(Out, L + 2, G, A0, A1, Accent(), 3.f);
		const FVector2D Tn(-U.Y, U.X);
		DrLine(Out, L + 2, G, { A1 + U * 14.f + Tn * 8.f, A1, A1 + U * 14.f - Tn * 8.f }, Accent(), 3.f);
		DrText(Out, L + 3, G, FString::Printf(TEXT("%.1f m/s"), Tr.WindMs), C + FVector2D(0, R * 0.45f), Font(12, "Bold"), Fg(), 0);
	};
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wiz.env_time")), Warn())];
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 12)[Chips(&T.TimeOfDay, { TEXT("day"), TEXT("dusk"), TEXT("night") }, { Tx(TEXT("time.day")), Tx(TEXT("time.dusk")), Tx(TEXT("time.night")) },
		{ ESmvIcon::Sun, ESmvIcon::Sunset, ESmvIcon::Moon })];
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wiz.env_weather")), Accent())];
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 12)[Chips(&T.Weather, { TEXT("clear"), TEXT("cloudy"), TEXT("rain"), TEXT("fog"), TEXT("snow") },
		{ Tx(TEXT("weather.clear")), Tx(TEXT("weather.cloudy")), Tx(TEXT("weather.rain")), Tx(TEXT("weather.fog")), Tx(TEXT("weather.snow")) },
		{ ESmvIcon::Sun, ESmvIcon::Cloud, ESmvIcon::Rain, ESmvIcon::Fog, ESmvIcon::Snow })];
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wiz.env_wind")), Accent3())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.wind")), Slider(&T.WindMs, 0, 30, 0.5, Ms))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.wind_dir")), Slider(&T.WindDir, 0, 355, 5, Deg))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.gust")), Slider(&T.WindGust, 0, 20, 0.5, Ms))];
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[SmvUi::Text(Tx(TEXT("wiz.env_note")), 11, "Regular", Mute())];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 24, 0)[Glass(V, FMargin(22.f))]
		+ SHorizontalBox::Slot().AutoWidth()[Glass(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Compass]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)[SmvUi::Text(Tx(TEXT("wiz.env_compass")), 11, "Regular", Dim(), false)], FMargin(20.f))];
}

TSharedRef<SWidget> SSmvShell::WizControl()
{
	SSmvShell* Self = this;
	USmvDataService* D = Data();
	TSharedRef<SUniformGridPanel> ModeGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(6.f)).MinDesiredSlotWidth(260.f);
	for (int32 m = 1; m <= 4; ++m)
	{
		TSharedRef<SSmvCanvas> Dia = SNew(SSmvCanvas).Size(FVector2D(240, 110));
		Dia->Painter = [m](const FGeometry& Gm, FSlateWindowElementList& Out, int32 L) { DrStickMode(Out, L, Gm, FVector2D::ZeroVector, FVector2D(Gm.GetLocalSize()), m, Accent()); };
		const bool bSel = D->Settings().StickMode == m;
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Selected(bSel)
			.Text(FText::FromString(FString::Printf(TEXT("Mode %d"), m))).SubText(FSmvI18n::T(FString::Printf(TEXT("stickmode.m%d"), m)))
			.OnClick([Self, m]()
			{
				if (!Self->Data()) return;
				Self->Data()->MutableSettings().StickMode = m;
				Self->Data()->MutableSettings().bStickMode2 = m == 2;
				Self->SaveSettingsQuiet();
				Self->Toast(FSmvI18n::Format(TEXT("stickmode.changed"), { { TEXT("m"), FString::FromInt(m) } }).ToString(), 6);
				Self->Refresh();
			})
			.Extra()[Dia];
		if (bSel) DefaultFocus = B;
		ModeGrid->AddSlot(m - 1, 0)[B];
	}
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("stickmode.title")))];
	V->AddSlot().AutoHeight()[ModeGrid];
	V->AddSlot().AutoHeight().Padding(6, 10, 0, 14)[SmvUi::Text(FText::FromString(StickKeyHint(D->Settings().StickMode)), 14, "Bold", Accent(), false)];
	V->AddSlot().AutoHeight().Padding(6, 0, 0, 14)[SmvUi::Text(Tx(TEXT("stickmode.hint")), 11, "Regular", Mute())];
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("settings.input")), Accent2())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.device")), Chips(&WizDevice, { TEXT("keyboard"), TEXT("gamepad"), TEXT("rc") },
		{ Tx(TEXT("device.keyboard")), Tx(TEXT("device.gamepad")), Tx(TEXT("device.rc")) }, { ESmvIcon::Keyboard, ESmvIcon::Gamepad, ESmvIcon::Stick }, [Self]()
		{
			if (!Self->Data()) return;
			Self->Data()->MutableSettings().InputDevice = Self->WizDevice;
			Self->SaveSettingsQuiet();
		}))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.expo")), Slider(&D->MutableSettings().StickExpo, 0, 1, 0.05, [](double X) { return FString::Printf(TEXT("%.0f%%"), X * 100.0); }, [Self]() { Self->SaveSettingsQuiet(); }))];
	return V;
}

TSharedRef<SWidget> SSmvShell::WizRules()
{
	SSmvShell* Self = this;
	USmvFlow* F = Flow.Get();
	FSmvFlightRequest& R = F->Draft;
	TSharedRef<SVerticalBox> L = SNew(SVerticalBox), Rt = SNew(SVerticalBox);
	// görev
	TArray<FString> Ids = { TEXT("-") }; TArray<FText> Ls = { Tx(TEXT("wizard.no_mission")) };
	for (const FSmvMission& X : Data()->Missions()) if (X.TrackId == R.Track.Id) { Ids.Add(X.Id); Ls.Add(FText::FromString(X.Name)); }
	L->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wizard.mission")), Accent2())];
	L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wizard.mission")), Cycle(&WizMissionId, Ids, Ls, [Self, F]()
	{
		F->Draft.bHasMission = Self->WizMissionId != TEXT("-");
		for (const FSmvMission& X : Self->Data()->Missions()) if (X.Id == Self->WizMissionId) F->Draft.Mission = X;
	}, 280.f))];
	// yarış
	L->AddSlot().AutoHeight().Padding(0, 14, 0, 8)[Section(Tx(TEXT("wizard.race")), Hex(0xf87171))];
	L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("race.kind")), Chips(&F->Race.Kind, { TEXT("none"), TEXT("rings"), TEXT("dogfight") },
		{ Tx(TEXT("race.none")), Tx(TEXT("race.rings")), Tx(TEXT("race.dogfight")) }, { ESmvIcon::Plane, ESmvIcon::Route, ESmvIcon::Match },
		[Self, F]() { if (F->Race.Kind == TEXT("dogfight")) F->Race.Bots = 1; Self->Refresh(); }))];
	if (F->Race.Kind != TEXT("none"))
	{
		if (F->Race.Kind == TEXT("rings"))
		{
			L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("race.bots")), SliderI(&F->Race.Bots, 1, 7))];
			L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("race.ring_count")), SliderI(&F->Race.RingCount, 1, 30))];
		}
		else L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("race.roles")), Chips(&F->Race.Roles, { TEXT("random"), TEXT("host_chaser"), TEXT("host_evader") },
			{ Tx(TEXT("race.role_random")), Tx(TEXT("race.role_chaser")), Tx(TEXT("race.role_evader")) }, {}))];
		L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("race.level")), Chips(&F->Race.Level, { TEXT("easy"), TEXT("normal"), TEXT("hard") },
			{ Tx(TEXT("level.easy")), Tx(TEXT("level.normal")), Tx(TEXT("level.hard")) }, {}))];
		L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("race.seed")), SliderI(&F->Race.Seed, 0, 999999, 1, [](double V) { return V <= 0 ? FSmvI18n::T(TEXT("race.seed_random")).ToString() : FString::FromInt((int32)V); }))];
		L->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(Tx(TEXT("race.local_note")), 10, "Regular", Mute())];
	}
	// hedefler (yalnız HUD + puan: kilit/angajman sayımı; güdüm yok)
	TargetCount = R.Targets.Num();
	Rt->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wizard.targets")), Warn())];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("targets.count")), SliderI(&TargetCount, 0, 8, 1, nullptr, [Self, F]()
	{
		F->Draft.Targets.Reset();
		for (int32 i = 0; i < Self->TargetCount; ++i)
		{
			FSmvTarget T; T.Model = Self->TargetModel; T.AltM = Self->TargetAlt; T.DiameterM = Self->TargetDia; T.SpeedMs = Self->TargetSpeed; T.bCw = (i % 2) == 0;
			T.EastM = (i % 3) * 150.0; T.NorthM = (i / 3) * 150.0;
			F->Draft.Targets.Add(T);
		}
	}))];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("targets.model")), Cycle(&TargetModel, { TEXT("tgt:sw300"), TEXT("tgt:lancet"), TEXT("tgt:altius"), TEXT("tgt:octopus") },
		{ FText::FromString(TEXT("Switchblade 300")), FText::FromString(TEXT("ZALA Lancet-3")), FText::FromString(TEXT("Altius-600M")), FText::FromString(TEXT("Octopus-100")) }, nullptr, 260.f))];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("targets.alt")), SliderF(&TargetAlt, 10, 3000, 10, [](double V) { return FString::Printf(TEXT("%.0f m"), V); }))];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("targets.diameter")), SliderF(&TargetDia, 50, 5000, 50, [](double V) { return FString::Printf(TEXT("%.0f m"), V); }))];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("targets.speed")), SliderF(&TargetSpeed, 1, 110, 1, [](double V) { return FString::Printf(TEXT("%.0f m/s"), V); }))];
	Rt->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(Tx(TEXT("targets.note")), 10, "Regular", Mute())];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 18, 0)[Glass(L, FMargin(20.f))]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Rt, FMargin(20.f))];
}

TSharedRef<SWidget> SSmvShell::WizAdvanced()
{
	SSmvShell* Self = this;
	USmvFlow* F = Flow.Get();
	FSmvFlightRequest& R = F->Draft;
	const FString& Ty = R.Drone.Type;
	auto Pw = [](double V) { return V < 0 ? FSmvI18n::T(TEXT("power.default")).ToString() : FString::Printf(TEXT("%.0f"), V); };
	TSharedRef<SVerticalBox> L = SNew(SVerticalBox), Rt = SNew(SVerticalBox);
	L->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wizard.launch")), Accent())];
	if (Ty == TEXT("plane"))
	{
		L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wizard.launch_method")), Chips(&R.Launch.Method, { TEXT("wheels"), TEXT("ramp"), TEXT("vehicle"), TEXT("hand") },
			{ Tx(TEXT("launch.wheels")), Tx(TEXT("launch.ramp")), Tx(TEXT("launch.vehicle")), Tx(TEXT("launch.hand")) }, {}, [Self]() { Self->Refresh(); }, 2))];
		if (R.Launch.Method == TEXT("ramp")) L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("launch.angle")), Slider(&R.Launch.AngleDeg, 10, 30, 1, [](double V) { return FString::Printf(TEXT("%.0f°"), V); }))];
		if (R.Launch.Method == TEXT("ramp") || R.Launch.Method == TEXT("vehicle")) L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("launch.release")), Slider(&R.Launch.ReleaseKmh, 40, 110, 1, [](double V) { return FString::Printf(TEXT("%.0f km/h"), V); }))];
		if (R.Launch.Method == TEXT("hand")) L->AddSlot().AutoHeight().Padding(0, 3)[SmvUi::Text(Tx(TEXT("launch.hand_note")), 10, "Regular", Mute())];
	}
	else if (Ty == TEXT("interceptor"))
	{
		L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wizard.launch_method")), Chips(&R.Launch.Method, { TEXT(""), TEXT("pod4"), TEXT("pod8"), TEXT("tube"), TEXT("handheld") },
			{ Tx(TEXT("launch.vertical")), Tx(TEXT("launch.pod4")), Tx(TEXT("launch.pod8")), Tx(TEXT("launch.tube")), Tx(TEXT("launch.handheld")) }, {}, [Self]() { Self->Refresh(); }, 3))];
		if (!R.Launch.Method.IsEmpty())
		{
			L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("launch.aim_az")), Slider(&R.Launch.AimAzDeg, 0, 355, 5, [](double V) { return FString::Printf(TEXT("%.0f°"), V); }))];
			L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("launch.aim_el")), Slider(&R.Launch.AimElDeg, 10, 85, 1, [](double V) { return FString::Printf(TEXT("%.0f°"), V); }))];
		}
	}
	else L->AddSlot().AutoHeight().Padding(0, 3)[SmvUi::Text(Tx(TEXT("wiz.launch_vertical")), 11, "Regular", Dim())];
	L->AddSlot().AutoHeight().Padding(0, 14, 0, 8)[Section(Tx(TEXT("hangar.power")), Warn())];
	if (Ty == TEXT("plane") || Ty == TEXT("vtol") || Ty == TEXT("interceptor")) L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("power.max_speed")), Slider(&R.Power.MaxSpeedMs, -1, 150, 1, Pw))];
	L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("power.fuel_l")), Slider(&R.Power.FuelL, -1, 500, 0.5, Pw))];
	L->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("power.mah")), Slider(&R.Power.ElecMah, -1, 200000, 100, Pw))];
	L->AddSlot().AutoHeight().Padding(0, 3)[SmvUi::Text(Tx(TEXT("power.wizard_hint")), 10, "Regular", Mute())];

	Rt->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wizard.sim")), Accent2())];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wizard.camera")), Chips(&R.Camera, { TEXT("day"), TEXT("thermal"), TEXT("night"), TEXT("none") },
		{ Tx(TEXT("cam.day")), Tx(TEXT("cam.thermal")), Tx(TEXT("cam.night")), Tx(TEXT("cam.none")) }, { ESmvIcon::Sun, ESmvIcon::Bolt, ESmvIcon::Moon, ESmvIcon::Camera }, nullptr, 2))];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wizard.speedup")), Chips(&WizSpeed, { TEXT("0.25"), TEXT("0.5"), TEXT("1.0"), TEXT("2.0"), TEXT("4.0"), TEXT("8.0") },
		{ FText::FromString(TEXT("0,25×")), FText::FromString(TEXT("0,5×")), FText::FromString(TEXT("1×")), FText::FromString(TEXT("2×")), FText::FromString(TEXT("4×")), FText::FromString(TEXT("8×")) }, {},
		[Self, F]() { F->Draft.Speedup = FCString::Atod(*Self->WizSpeed); }, 3))];
	const bool bIn = Client() && Client()->IsLoggedIn();
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wizard.backend")), bIn
		? Chips(&WizBackend, { TEXT("local"), TEXT("cloud") }, { Tx(TEXT("backend.local")), Tx(TEXT("backend.cloud")) }, { ESmvIcon::Monitor, ESmvIcon::Cloud },
			[Self, F]() { F->Draft.Backend = Self->WizBackend == TEXT("cloud") ? ESmvFlightBackend::Cloud : ESmvFlightBackend::LocalSitl; })
		: SmvUi::Text(Tx(TEXT("backend.local_guest")), 12, "Regular", Fg()))];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wiz.record")), Toggle(&R.bRecordBin))];
	Rt->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SmvUi::Text(USmvLocalFlightLink::SitlAvailable() ? Tx(TEXT("wizard.sitl_note")) : Tx(TEXT("menu.sitl_missing")), 10, "Regular", Mute())];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 18, 0)[Glass(L, FMargin(20.f))]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Rt, FMargin(20.f))];
}

TSharedRef<SWidget> SSmvShell::WizSummary()
{
	USmvFlow* F = Flow.Get();
	const FSmvFlightRequest& R = F->Draft;
	const int32 Sm = Data()->Settings().StickMode;
	auto Line = [](ESmvIcon I, const FText& K, const FString& V) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 6, 12, 6)[Icon(I, 22.f, Accent())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[SNew(SBox).WidthOverride(170.f)[SmvUi::Text(K, 12, "Regular", Dim(), false)]]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(V), 13, "Bold", Fg(), false)];
	};
	FString Race = FSmvI18n::T(FString(TEXT("race.")) + F->Race.Kind).ToString();
	if (F->Race.Kind == TEXT("rings")) Race += FString::Printf(TEXT(" · %d YZ · %s"), F->Race.Bots, *FSmvI18n::T(FString(TEXT("level.")) + F->Race.Level).ToString());
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[Line(IconForType(R.Drone.Type), Tx(TEXT("wizard.drone")), FString::Printf(TEXT("%s · %s"), *R.Drone.Name, *FSmvI18n::T(FString(TEXT("type.")) + R.Drone.Type).ToString()))];
	V->AddSlot().AutoHeight()[Line(ESmvIcon::Pilot, Tx(TEXT("wizard.pilot")), R.Pilot.Name)];
	V->AddSlot().AutoHeight()[Line(ESmvIcon::Map, Tx(TEXT("wizard.track")), FString::Printf(TEXT("%s · %s"), *R.Track.Name, *FSmvI18n::T(FString(TEXT("world.")) + (FSmvFabContent::IsFabWorld(R.Track.WorldMode) ? FString(TEXT("fab")) : R.Track.WorldMode)).ToString()))];
	V->AddSlot().AutoHeight()[Line(ESmvIcon::Sun, Tx(TEXT("wiz.s_env")), FString::Printf(TEXT("%s · %s · %.1f m/s %.0f°"), *FSmvI18n::T(FString(TEXT("time.")) + R.Track.TimeOfDay).ToString(),
		*FSmvI18n::T(FString(TEXT("weather.")) + R.Track.Weather).ToString(), R.Track.WindMs, R.Track.WindDir))];
	V->AddSlot().AutoHeight()[Line(ESmvIcon::Gamepad, Tx(TEXT("stickmode.title")), FString::Printf(TEXT("Mode %d · %s"), Sm, *FSmvI18n::T(FString(TEXT("device.")) + Data()->Settings().InputDevice).ToString()))];
	V->AddSlot().AutoHeight()[Line(ESmvIcon::Mission, Tx(TEXT("wizard.mission")), R.bHasMission ? R.Mission.Name : Tx(TEXT("wizard.no_mission")).ToString())];
	V->AddSlot().AutoHeight()[Line(ESmvIcon::Route, Tx(TEXT("wizard.race")), Race)];
	V->AddSlot().AutoHeight()[Line(ESmvIcon::Camera, Tx(TEXT("wizard.camera")), FSmvI18n::T(FString(TEXT("cam.")) + R.Camera).ToString())];
	V->AddSlot().AutoHeight()[Line(ESmvIcon::Bolt, Tx(TEXT("wizard.speedup")), FString::Printf(TEXT("%.2g×"), R.Speedup))];
	if (R.Targets.Num()) V->AddSlot().AutoHeight()[Line(ESmvIcon::Shield, Tx(TEXT("wizard.targets")), FString::FromInt(R.Targets.Num()))];
	const FString Err = F->ValidateDraft();
	if (!Err.IsEmpty()) V->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[SmvUi::Text(FText::FromString(Err), 13, "Bold", Danger())];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 22, 0)[Glass(V, FMargin(24.f))]
		+ SHorizontalBox::Slot().AutoWidth()[PreviewCard(R.Drone.Type, R.Drone.ModelKey, R.Drone.Color, 480.f, 300.f)];
}

void SSmvShell::StartFromWizard()
{
	if (!Flow.IsValid()) return;
	USmvFlow* F = Flow.Get();
	F->Draft.Targets.Reset();
	for (int32 i = 0; i < TargetCount; ++i)
	{
		FSmvTarget T; T.Model = TargetModel; T.AltM = TargetAlt; T.DiameterM = TargetDia; T.SpeedMs = TargetSpeed; T.bCw = (i % 2) == 0;
		T.EastM = (i % 3) * 150.0; T.NorthM = (i / 3) * 150.0;
		F->Draft.Targets.Add(T);
	}
	const FString Err = F->ValidateDraft();
	if (!Err.IsEmpty()) { Toast(Err, 3); return; }
	SaveSettingsQuiet();
	FSmvUiSound::Play(ESmvSnd::Open);
	F->StartFlight();
}

// ---------------------------------------------------------------------------
// yükleniyor / uçuş (duraklatma) / özet
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Loading()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	TSharedRef<SSmvCanvas> Radar = SNew(SSmvCanvas).Size(FVector2D(220, 220));
	Radar->Painter = [](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D C = FVector2D(G.GetLocalSize()) * 0.5;
		const float R = (float)C.X - 8.f;
		const double T = FPlatformTime::Seconds();
		DrGlow(Out, L, G, C, R * 1.2f, Accent().CopyWithNewOpacity(0.25f));
		DrBox(Out, L + 1, G, C - FVector2D(R, R), FVector2D(2 * R, 2 * R), Hex(0x06101e, 0.9f), Circle());
		for (int32 k = 1; k <= 3; ++k) DrArc(Out, L + 2, G, C, R * k / 3.f, 0.f, 360.f, Accent().CopyWithNewOpacity(0.25f), 1.f);
		DrSeg(Out, L + 2, G, C - FVector2D(R, 0), C + FVector2D(R, 0), Accent().CopyWithNewOpacity(0.15f), 1.f);
		DrSeg(Out, L + 2, G, C - FVector2D(0, R), C + FVector2D(0, R), Accent().CopyWithNewOpacity(0.15f), 1.f);
		const float A = (float)FMath::Fmod(T * 120.0, 360.0);
		for (int32 k = 0; k < 24; ++k)
		{
			const float Aa = FMath::DegreesToRadians(A - k * 2.5f);
			DrSeg(Out, L + 3, G, C, C + FVector2D(FMath::Cos(Aa), FMath::Sin(Aa)) * R, Accent().CopyWithNewOpacity(0.5f * (1.f - k / 24.f)), 2.f);
		}
		for (int32 k = 0; k < 5; ++k)
		{
			const float Ba = k * 1.3f + 0.4f, Br = R * (0.3f + 0.13f * k);
			const FVector2D P = C + FVector2D(FMath::Cos(Ba), FMath::Sin(Ba)) * Br;
			const float Age = FMath::Fmod(A - FMath::RadiansToDegrees(Ba) + 720.f, 360.f) / 360.f;
			DrBox(Out, L + 4, G, P - FVector2D(3, 3), FVector2D(6, 6), Accent3().CopyWithNewOpacity(FMath::Max(0.f, 1.f - Age * 1.5f)), Circle());
		}
	};
	static const TCHAR* TipKeys[] = { TEXT("tip.1"), TEXT("tip.2"), TEXT("tip.3"), TEXT("tip.4"), TEXT("tip.5"), TEXT("tip.6"), TEXT("tip.7"), TEXT("tip.8") };
	TSharedRef<SSmvButton> Cancel = SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Back).Text(Tx(TEXT("common.cancel"))).OnClick([F]() { if (F.IsValid()) F->EndFlight(TEXT("abort")); });
	DefaultFocus = Cancel;
	return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(680.f)
		[
			Glass(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Radar]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 16, 0, 0)[Title(Tx(TEXT("loading.title")), 22)]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10)
				[
					SNew(STextBlock).Text_Lambda([F]()
					{
						const int32 Dots = (int32)(FPlatformTime::Seconds() * 2.0) % 4;
						return FText::FromString((F.IsValid() ? F->LoadingText : FString()) + FString::ChrN(Dots, TEXT('.')));
					}).Font(Font(13)).ColorAndOpacity(Fg()).AutoWrapText(true).Justification(ETextJustify::Center)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Dim()).AutoWrapText(true).Justification(ETextJustify::Center)
					.Text_Lambda([]() { const int32 I = (int32)(FPlatformTime::Seconds() / 6.0) % 8; return FSmvI18n::T(TipKeys[I]); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 20, 0, 0)[Cancel], FMargin(32.f))
		]
	];
}

TSharedRef<SWidget> SSmvShell::FlightPause()
{
	if (!GameMode.IsValid() || !GameMode->IsPausedMenu()) return SNullWidget::NullWidget;
	TWeakObjectPtr<ASmvGameMode> G = GameMode;
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	static FString PauseMode;
	PauseMode = Data() ? FString::FromInt(Data()->Settings().StickMode) : FString(TEXT("2"));
	TSharedRef<SSmvButton> Resume = SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Play).MinWidth(420.f).Text(Tx(TEXT("pause.resume"))).OnClick([G]() { if (G.IsValid()) G->TogglePause(); });
	DefaultFocus = Resume;
	auto Wide = [](TSharedRef<SWidget> W) { return SNew(SBox).Padding(FMargin(0, 4))[W]; };
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	B->AddSlot().AutoHeight().HAlign(HAlign_Center)[Title(Tx(TEXT("pause.title")), 26)];
	B->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 16)[SmvUi::Text(Tx(TEXT("pause.note")), 11, "Regular", Mute(), false)];
	B->AddSlot().AutoHeight()[Wide(Resume)];
	B->AddSlot().AutoHeight()[Wide(SNew(SSmvButton).Icon(ESmvIcon::Home).MinWidth(420.f).Text(Tx(TEXT("pause.rtl"))).OnClick([G]() { if (G.IsValid()) { G->Command(TEXT("rtl")); G->TogglePause(); } }))];
	B->AddSlot().AutoHeight()[Wide(SNew(SSmvButton).Icon(ESmvIcon::Replay).MinWidth(420.f).Text(Tx(TEXT("pause.reset"))).OnClick([G]() { if (G.IsValid()) { G->Command(TEXT("reset")); G->TogglePause(); } }))];
	B->AddSlot().AutoHeight()[Wide(SNew(SSmvButton).Icon(ESmvIcon::Keyboard).MinWidth(420.f).Text(Tx(TEXT("menu.help"))).OnClick([Self]() { Self->ToggleHelp(); }))];
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 6)[Section(Tx(TEXT("pause.quick")), Accent2())];
	B->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("stickmode.title")), Cycle(&PauseMode, { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4") },
		{ FText::FromString(TEXT("Mode 1")), FText::FromString(TEXT("Mode 2")), FText::FromString(TEXT("Mode 3")), FText::FromString(TEXT("Mode 4")) }, [Self, G]()
		{
			if (!Self->Data()) return;
			const int32 M = FCString::Atoi(*PauseMode);
			Self->Data()->MutableSettings().StickMode = M;
			Self->Data()->MutableSettings().bStickMode2 = M == 2;
			Self->SaveSettingsQuiet();
			if (G.IsValid()) G->ApplyUiPrefs();
		}, 200.f))];
	if (Data()) B->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.volume")), Slider(&Data()->MutableSettings().Volume, 0, 1, 0.05, [](double V) { return FString::Printf(TEXT("%.0f%%"), V * 100.0); },
		[Self, G]() { if (G.IsValid()) G->ApplyUiPrefs(); }))];
	B->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("bind.cockpit")), Toggle(&FSmvUiPrefs::Get().bCockpit, []() { FSmvUiPrefs::Get().Save(); }))];
	B->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(bPauseAdvanced ? ESmvIcon::Prev : ESmvIcon::Next).Text(Tx(TEXT("pause.advanced")))
		.OnClick([Self]() { Self->bPauseAdvanced = !Self->bPauseAdvanced; Self->Refresh(); })];
	if (bPauseAdvanced)
	{
		B->AddSlot().AutoHeight().Padding(0, 6, 0, 4)[Section(Tx(TEXT("servo.title")), Warn())];
		B->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("servo.ch")), SliderI(&ServoCh, 1, 16))];
		B->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("servo.pwm")), SliderI(&ServoPwm, 800, 2200, 10, [](double V) { return FString::Printf(TEXT("%d µs"), (int32)V); }))];
		B->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("servo.cmp")), Chips(&ServoCmp, { TEXT("ge"), TEXT("le"), TEXT("eq") }, { FText::FromString(TEXT("≥")), FText::FromString(TEXT("≤")), FText::FromString(TEXT("=")) }, {}))];
		B->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("servo.delay")), SliderI(&ServoDelay, 0, 60000, 100, [](double V) { return FString::Printf(TEXT("%d ms"), (int32)V); }))];
		B->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("servo.listen")), [G, Self]()
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetBoolField(TEXT("on"), true); P->SetNumberField(TEXT("ch"), Self->ServoCh); P->SetNumberField(TEXT("pwm"), Self->ServoPwm);
				P->SetStringField(TEXT("cmp"), Self->ServoCmp); P->SetNumberField(TEXT("tol"), 50); P->SetNumberField(TEXT("delay_ms"), Self->ServoDelay);
				if (G.IsValid()) G->Command(TEXT("servo_trigger"), P);
			}, false, true, ESmvIcon::Volume)]
			+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("servo.off")), [G]()
			{
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetBoolField(TEXT("on"), false);
				if (G.IsValid()) G->Command(TEXT("servo_trigger"), P);
			})]
		];
		B->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SNew(STextBlock).Text_Lambda([G]() { return FText::FromString(G.IsValid() ? G->MissionPlannerLine() : FString()); }).Font(Font(10)).ColorAndOpacity(Dim()).AutoWrapText(true)];
	}
	B->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Power).MinWidth(420.f).Text(Tx(TEXT("pause.end")))
		.OnClick([F, G]() { if (G.IsValid() && G->IsPausedMenu()) G->TogglePause(); if (F.IsValid()) F->EndFlight(TEXT("user")); })];
	B->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12, 0, 0)[HintBar({ { TEXT("Esc"), Tx(TEXT("pause.resume")) }, { TEXT("F1"), Tx(TEXT("hint.help")) } },
		{ { TEXT("pad:Menu"), Tx(TEXT("pause.resume")) }, { TEXT("pad:A"), Tx(TEXT("hint.select")) } })];
	return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.03f, 0.55f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(560.f).MaxDesiredHeight(980.f)
		[
			Glass(SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[B], FMargin(28.f, 24.f))
		]
	];
}

TSharedRef<SWidget> SSmvShell::Summary()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	const FSmvFlightSummary& S = Flow->LastSummary;
	const FLinearColor ResC = S.Result == TEXT("ok") ? Accent3() : S.Result == TEXT("crash") ? Danger() : Warn();
	auto Tile = [](const FText& K, const FString& V, const FLinearColor& C, ESmvIcon I) -> TSharedRef<SWidget>
	{
		return Glass(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Icon(I, 24.f, C)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[SNew(STextBlock).Text(FText::FromString(V)).Font(Font(24, "Bold")).ColorAndOpacity(Fg())]
			+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(K, 11, "Regular", Dim(), false)], FMargin(18.f, 14.f));
	};
	TSharedRef<SUniformGridPanel> G = SNew(SUniformGridPanel).SlotPadding(FMargin(6.f)).MinDesiredSlotWidth(200.f);
	int32 i = 0;
	auto Add = [&G, &i](TSharedRef<SWidget> W) { G->AddSlot(i % 4, i / 4)[W]; ++i; };
	Add(Tile(Tx(TEXT("summary.duration")), FString::Printf(TEXT("%02d:%02d"), (int32)S.DurationS / 60, (int32)S.DurationS % 60), Accent(), ESmvIcon::Replay));
	Add(Tile(Tx(TEXT("summary.distance")), FString::Printf(TEXT("%.2f km"), S.DistM / 1000.0), Accent3(), ESmvIcon::Route));
	Add(Tile(Tx(TEXT("summary.max_speed")), FString::Printf(TEXT("%.0f km/h"), S.MaxSpeedMs * 3.6), Warn(), ESmvIcon::Bolt));
	if (S.AlertScore >= 0) Add(Tile(Tx(TEXT("summary.discipline")), FString::Printf(TEXT("%s · %d"), *S.AlertGrade, S.AlertScore), S.AlertScore >= 75 ? Accent3() : Warn(), ESmvIcon::Shield));
	if (S.GatesPassed || S.GatesMissed) Add(Tile(Tx(TEXT("summary.gates")), FString::Printf(TEXT("%d / %d"), S.GatesPassed, S.GatesPassed + S.GatesMissed), Hex(0xf472b6), ESmvIcon::Flag));
	if (S.BestLapS > 0) Add(Tile(Tx(TEXT("summary.best_lap")), FString::Printf(TEXT("%.2f s"), S.BestLapS), Accent2(), ESmvIcon::Trophy));
	if (S.FuelUsedL > 0) Add(Tile(Tx(TEXT("summary.fuel")), FString::Printf(TEXT("%.2f L"), S.FuelUsedL), Warn(), ESmvIcon::Bolt));
	if (S.WhUsed > 0) Add(Tile(Tx(TEXT("summary.energy")), FString::Printf(TEXT("%.1f Wh"), S.WhUsed), Accent(), ESmvIcon::Bolt));
	if (S.TargetScore > 0) Add(Tile(Tx(TEXT("summary.targets")), FString::FromInt(S.TargetScore), Danger(), ESmvIcon::Shield));
	if (S.AlertScore >= 0) Add(Tile(Tx(TEXT("summary.alerts")), FString::Printf(TEXT("%d W · %d C"), S.Warnings, S.Cautions), Warn(), ESmvIcon::Info));
	TSharedRef<SHorizontalBox> Badges = SNew(SHorizontalBox);
	for (const FString& Bd : S.Badges)
	{
		Badges->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SBorder).BorderImage(Round(14.f)).BorderBackgroundColor(Warn().CopyWithNewOpacity(0.2f)).Padding(FMargin(12.f, 6.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[Icon(ESmvIcon::Star, 16.f, Warn())]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(Bd), 12, "Bold", Fg(), false)]
			]
		];
	}
	TSharedRef<SSmvButton> Again = SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Play).Text(Tx(TEXT("summary.again"))).OnClick([F]() { if (F.IsValid()) F->StartFlight(); });
	DefaultFocus = Again;
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	B->AddSlot().AutoHeight()
	[
		SNew(SBorder).BorderImage(Round(18.f)).BorderBackgroundColor(ResC.CopyWithNewOpacity(0.16f)).Padding(FMargin(24.f, 18.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 18, 0)[Icon(S.Result == TEXT("ok") ? ESmvIcon::Check : ESmvIcon::Info, 46.f, ResC)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Title(FSmvI18n::T(FString(TEXT("result.")) + S.Result), 30, ResC)]
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *F->Draft.Drone.Name, *F->Draft.Track.Name) + (S.EndReason.IsEmpty() ? FString() : FString::Printf(TEXT(" · %s"), *S.EndReason))), 12, "Regular", Dim(), false)]
			]
		]
	];
	B->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[G];
	if (S.Badges.Num()) B->AddSlot().AutoHeight().Padding(6, 12, 0, 0)[Badges];
	if (S.bServoTriggerFired) B->AddSlot().AutoHeight().Padding(6, 12, 0, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %.1f m AGL · %.0f m"), *Tx(TEXT("summary.servo_fired")).ToString(), S.ServoTriggerAglM, S.ServoTriggerDistM)), 12, "Bold", Warn())];
	B->AddSlot().AutoHeight().Padding(6, 12, 0, 0)[SmvUi::Text(Client() && Client()->IsLoggedIn() && F->Draft.Backend == ESmvFlightBackend::Cloud ? Tx(TEXT("summary.cloud_saved")) : Tx(TEXT("summary.local_saved")), 10, "Regular", Mute())];
	B->AddSlot().AutoHeight().Padding(0, 18, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Again]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[SNew(SSmvButton).Icon(ESmvIcon::Gear).Text(Tx(TEXT("summary.change"))).OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Wizard); })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[SNew(SSmvButton).Icon(ESmvIcon::Replay).Text(Tx(TEXT("menu.logbook"))).OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Logbook); })]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Icon(ESmvIcon::Home).Text(Tx(TEXT("summary.menu"))).OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Menu); })]
	];
	return Page(Tx(TEXT("summary.title")), Tx(TEXT("summary.sub")), ESmvIcon::Flag, SNew(SBox).HAlign(HAlign_Left)[SNew(SBox).WidthOverride(940.f)[B]], false);
}

// ---------------------------------------------------------------------------
// karşılaşma salonu
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Lobby()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	if (!(Client() && Client()->IsLoggedIn()))
	{
		TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
		B->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("lobby.login_needed")), 14, "Regular", Fg())];
		B->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Btn(Tx(TEXT("lobby.local_race")), [F]() { if (F.IsValid()) { F->NewDraft(); F->Race.Kind = TEXT("rings"); F->SetScreen(ESmvScreen::Wizard); } }, true, true, ESmvIcon::Route)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Btn(Tx(TEXT("lobby.local_dogfight")), [F]() { if (F.IsValid()) { F->NewDraft(); F->Race.Kind = TEXT("dogfight"); F->Race.Bots = 1; F->SetScreen(ESmvScreen::Wizard); } }, false, true, ESmvIcon::Match)]
			+ SHorizontalBox::Slot().AutoWidth()[Btn(Tx(TEXT("welcome.login")), [F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Login); }, false, true, ESmvIcon::User)]
		];
		return Page(Tx(TEXT("menu.match")), Tx(TEXT("menu.match_sub")), ESmvIcon::Match, SNew(SBox).HAlign(HAlign_Left)[SNew(SBox).WidthOverride(820.f)[Glass(B, FMargin(26.f))]]);
	}
	F->EnsureMatchClient();
	USmvMatchClient* M = F->GetMatch();
	if (M && M->HasMatch())
	{
		const FSmvMatch& X = M->Current();
		TSharedRef<SVerticalBox> P = SNew(SVerticalBox);
		static const uint32 Cols[] = { 0x38bdf8, 0xf472b6, 0x34d399, 0xfbbf24, 0xa78bfa, 0xfb923c, 0xf87171, 0x94a3b8 };
		int32 k = 0;
		for (const FSmvMatchPlayer& Pl : X.Players)
		{
			const FLinearColor C = Hex(Cols[k++ % 8]);
			P->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Card()).Padding(FMargin(14.f, 8.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Icon(Pl.bBot ? ESmvIcon::Drone : ESmvIcon::Pilot, 24.f, C)]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(Pl.Name + (Pl.bBot ? FString(TEXT("  · AI")) : FString()) + (Pl.bHost ? FString(TEXT("  ★")) : FString())), 13, "Bold", Fg(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0)[SmvUi::Text(FText::FromString(Pl.Rings ? FString::Printf(TEXT("%d/%d"), Pl.Rings, Pl.Total) : FString()), 12, "Regular", Dim(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor((Pl.bReady ? Accent3() : Warn()).CopyWithNewOpacity(0.22f)).Padding(FMargin(10.f, 3.f))
						[
							SmvUi::Text(Pl.bReady ? Tx(TEXT("lobby.ready")) : Tx(TEXT("lobby.not_ready")), 11, "Bold", Pl.bReady ? Accent3() : Warn(), false)
						]
					]
				]
			];
		}
		TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
		B->AddSlot().AutoHeight()[Title(FText::FromString(FString::Printf(TEXT("%s · %s"), *X.Code, *FSmvI18n::T(FString(TEXT("race.")) + X.Kind).ToString())), 22)];
		B->AddSlot().AutoHeight().Padding(0, 4, 0, 14)[SmvUi::Text(FText::FromString(X.State), 12, "Regular", Dim(), false)];
		B->AddSlot().AutoHeight()[P];
		B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("lobby.ready_btn")), [F]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->SetReady(true); }, true, true, ESmvIcon::Check)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("lobby.start")), [F]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->Start(); }, false, TAttribute<bool>(X.bCanStart), ESmvIcon::Play)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("lobby.add_bot")), [F, Self]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->AddBot(Self->MatchLevel); }, false, true, ESmvIcon::Plus)]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Back).Text(Tx(TEXT("lobby.leave"))).OnClick([F]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->Leave(); })]
		];
		B->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[SmvUi::Text(FSmvI18n::Format(TEXT("lobby.invite"), { { TEXT("code"), X.Code } }), 12, "Regular", Accent())];
		if (!M->LastError.IsEmpty()) B->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SmvUi::Text(FText::FromString(M->LastError), 11, "Regular", Danger())];
		return Page(Tx(TEXT("menu.match")), Tx(TEXT("lobby.room")), ESmvIcon::Match, SNew(SBox).HAlign(HAlign_Left)[SNew(SBox).WidthOverride(860.f)[Glass(B, FMargin(26.f))]]);
	}
	TSharedRef<SVerticalBox> C1 = SNew(SVerticalBox), C2 = SNew(SVerticalBox), C3 = SNew(SVerticalBox);
	C1->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("lobby.create")))];
	C1->AddSlot().AutoHeight().Padding(0, 3)[Chips(&MatchKind, { TEXT("rings"), TEXT("dogfight") }, { Tx(TEXT("race.rings")), Tx(TEXT("race.dogfight")) }, { ESmvIcon::Route, ESmvIcon::Match })];
	C1->AddSlot().AutoHeight().Padding(0, 6, 0, 3)[SmvUi::Text(Tx(TEXT("lobby.password")), 11, "Bold", Dim())];
	C1->AddSlot().AutoHeight()[TextEdit(&MatchPassword, true, FText::GetEmpty(), 240.f)];
	C1->AddSlot().AutoHeight().Padding(0, 6, 0, 3)[SmvUi::Text(Tx(TEXT("lobby.max")), 11, "Bold", Dim())];
	C1->AddSlot().AutoHeight()[SliderI(&MatchMax, 2, 8)];
	C1->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Btn(Tx(TEXT("lobby.create_btn")), [F, Self]()
	{
		if (F.IsValid() && F->GetMatch()) F->GetMatch()->Create(F->Draft, Self->MatchKind, Self->MatchPassword, Self->MatchKind == TEXT("dogfight") ? 2 : Self->MatchMax, nullptr, nullptr);
	}, true, true, ESmvIcon::Plus)];
	C2->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("lobby.ai")), Accent2())];
	C2->AddSlot().AutoHeight().Padding(0, 3)[Chips(&MatchLevel, { TEXT("easy"), TEXT("normal"), TEXT("hard") }, { Tx(TEXT("level.easy")), Tx(TEXT("level.normal")), Tx(TEXT("level.hard")) }, {})];
	C2->AddSlot().AutoHeight().Padding(0, 6, 0, 3)[SmvUi::Text(Tx(TEXT("race.bots")), 11, "Bold", Dim())];
	C2->AddSlot().AutoHeight()[SliderI(&MatchBots, 1, 7)];
	C2->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Btn(Tx(TEXT("lobby.ai_btn")), [F, Self]()
	{
		if (F.IsValid() && F->GetMatch()) F->GetMatch()->CreateAi(F->Draft, Self->MatchKind, Self->MatchLevel, Self->MatchKind == TEXT("dogfight") ? 1 : Self->MatchBots, nullptr, nullptr);
	}, false, true, ESmvIcon::Drone)];
	C3->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("lobby.join")), Accent3())];
	C3->AddSlot().AutoHeight().Padding(0, 3, 0, 3)[SmvUi::Text(Tx(TEXT("lobby.code")), 11, "Bold", Dim())];
	C3->AddSlot().AutoHeight()[TextEdit(&MatchCode, false, FText::GetEmpty(), 240.f)];
	C3->AddSlot().AutoHeight().Padding(0, 6, 0, 3)[SmvUi::Text(Tx(TEXT("lobby.password")), 11, "Bold", Dim())];
	C3->AddSlot().AutoHeight()[TextEdit(&MatchPassword, true, FText::GetEmpty(), 240.f)];
	C3->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Btn(Tx(TEXT("lobby.join_btn")), [F, Self]() { if (F.IsValid() && F->GetMatch()) F->GetMatch()->Join(Self->MatchCode, Self->MatchPassword); }, false, true, ESmvIcon::Next)];
	TSharedRef<SVerticalBox> All = SNew(SVerticalBox);
	All->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 14, 0)[Glass(C1, FMargin(20.f))]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 14, 0)[Glass(C2, FMargin(20.f))]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(C3, FMargin(20.f))]
	];
	All->AddSlot().AutoHeight().Padding(6, 10, 0, 0)[SmvUi::Text(Tx(TEXT("lobby.uses_draft")), 10, "Regular", Mute())];
	if (M && !M->LastError.IsEmpty()) All->AddSlot().AutoHeight().Padding(6, 8, 0, 0)[SmvUi::Text(FText::FromString(M->LastError), 11, "Regular", Danger())];
	return Page(Tx(TEXT("menu.match")), Tx(TEXT("menu.match_sub")), ESmvIcon::Match, All);
}

// ---------------------------------------------------------------------------
// TOP 10 / kayıtlar
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Leaderboard()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	static FString Period;
	Period = F->LeaderboardPeriod;
	B->AddSlot().AutoHeight().Padding(0, 0, 0, 14)[Chips(&Period, { TEXT("30d"), TEXT("all") }, { Tx(TEXT("top10.30d")), Tx(TEXT("top10.all")) }, { ESmvIcon::Sun, ESmvIcon::Trophy },
		[F]() { if (F.IsValid()) { F->LeaderboardPeriod = Period; F->FetchLeaderboard(); } })];
	int32 MaxScore = 1;
	for (const FSmvLeaderRow& R : Flow->Leaderboard) MaxScore = FMath::Max(MaxScore, R.Score);
	for (const FSmvLeaderRow& R : Flow->Leaderboard)
	{
		const FLinearColor Medal = R.Rank == 1 ? Hex(0xfbbf24) : R.Rank == 2 ? Hex(0xcbd5e1) : R.Rank == 3 ? Hex(0xd97706) : Mute();
		const float Frac = (float)R.Score / MaxScore;
		TSharedRef<SSmvCanvas> Bar = SNew(SSmvCanvas).Size(FVector2D(200, 8));
		Bar->Painter = [Frac](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			const FVector2D S = FVector2D(G.GetLocalSize());
			DrBox(Out, L, G, FVector2D::ZeroVector, S, FLinearColor(1, 1, 1, 0.06f), Round(4.f));
			DrBox(Out, L + 1, G, FVector2D::ZeroVector, FVector2D(S.X * Frac, S.Y), Accent(), Round(4.f));
		};
		B->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SBorder).BorderImage(Round(14.f)).BorderBackgroundColor(R.Rank <= 3 ? Medal.CopyWithNewOpacity(0.10f) : Card()).Padding(FMargin(16.f, 10.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)
				[
					SNew(SBox).WidthOverride(38.f).HeightOverride(38.f)
					[
						SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(Medal.CopyWithNewOpacity(R.Rank <= 3 ? 0.9f : 0.25f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::AsNumber(R.Rank)).Font(Font(14, "Bold")).ColorAndOpacity(R.Rank <= 3 ? Hex(0x111827) : Fg())
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(R.Name + (R.bDemo ? FSmvI18n::T(TEXT("top10.demo")).ToString() : FString())), 14, "Bold", Fg(), false)]
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FSmvI18n::Format(TEXT("top10.row"), { { TEXT("w"), FString::FromInt(R.Wins) }, { TEXT("m"), FString::FromInt(R.Matches) }, { TEXT("f"), FString::FromInt(R.Flights) } }), 11, "Regular", Dim(), false)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16, 0)[Bar]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).MinDesiredWidth(90.f).HAlign(HAlign_Right)[SNew(STextBlock).Text(FText::AsNumber(R.Score)).Font(Font(18, "Bold")).ColorAndOpacity(Accent())]]
			]
		];
	}
	if (Flow->Leaderboard.Num() == 0) B->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("top10.empty")), 12, "Regular", Mute())];
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[SmvUi::Text(Tx(TEXT("top10.note")), 10, "Regular", Mute())];
	return Page(Tx(TEXT("menu.top10")), Tx(TEXT("top10.sub")), ESmvIcon::Trophy, SNew(SBox).HAlign(HAlign_Left)[SNew(SBox).WidthOverride(980.f)[B]]);
}

TSharedRef<SWidget> SSmvShell::Logbook()
{
	TSharedRef<SVerticalBox> B = SNew(SVerticalBox);
	if (Data())
	{
		for (const FSmvFlightSummary& S : Data()->Flights())
		{
			const FLinearColor C = S.Result == TEXT("ok") ? Accent3() : S.Result == TEXT("crash") ? Danger() : Warn();
			B->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Card()).Padding(FMargin(16.f, 10.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
					[
						SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(C.CopyWithNewOpacity(0.2f)).Padding(FMargin(10.f, 3.f))
						[
							SmvUi::Text(FSmvI18n::T(FString(TEXT("result.")) + S.Result), 11, "Bold", C, false)
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(S.FlightId), 12, "Regular", Fg(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%02d:%02d"), (int32)S.DurationS / 60, (int32)S.DurationS % 60)), 13, "Bold", Fg(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%.2f km"), S.DistM / 1000.0)), 13, "Bold", Fg(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0)[SmvUi::Text(FText::FromString(S.AlertScore >= 0 ? FString::Printf(TEXT("%s %d"), *S.AlertGrade, S.AlertScore) : FString(TEXT("—"))), 13, "Bold", Accent(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 0, 0)[SmvUi::Text(FText::FromString(S.GatesPassed ? FString::Printf(TEXT("◎ %d"), S.GatesPassed) : FString()), 13, "Bold", Hex(0xf472b6), false)]
				]
			];
		}
		if (Data()->Flights().Num() == 0) B->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("logbook.empty")), 13, "Regular", Mute())];
	}
	B->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s: %s"), *FSmvI18n::T(TEXT("logbook.bin")).ToString(),
		*FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("Logs"))))), 10, "Regular", Mute())];
	return Page(Tx(TEXT("menu.logbook")), Tx(TEXT("menu.logbook_sub")), ESmvIcon::Replay, SNew(SBox).HAlign(HAlign_Left)[SNew(SBox).WidthOverride(1020.f)[B]]);
}

// ---------------------------------------------------------------------------
// ayarlar (sekmeli)
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::Settings()
{
	SSmvShell* Self = this;
	const TArray<FText> TabL = { Tx(TEXT("set.t_general")), Tx(TEXT("set.t_display")), Tx(TEXT("set.t_audio")), Tx(TEXT("set.t_controls")), Tx(TEXT("set.t_keys")), Tx(TEXT("set.t_alerts")), Tx(TEXT("set.t_link")) };
	const TArray<ESmvIcon> TabI = { ESmvIcon::Globe, ESmvIcon::Monitor, ESmvIcon::Volume, ESmvIcon::Gamepad, ESmvIcon::Keyboard, ESmvIcon::Shield, ESmvIcon::Cloud };
	TSharedRef<SWidget> TabW = Tabs(TabL, TabI, &SetTab);
	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	switch (SetTab)
	{
	case 0: Body = SetGeneral(); break;
	case 1: Body = SetDisplay(); break;
	case 2: Body = SetAudio(); break;
	case 3: Body = SetControls(); break;
	case 4: Body = SetKeys(); break;
	case 5: Body = SetAlerts(); break;
	default: Body = SetLink(); break;
	}
	TSharedRef<SWidget> Save = SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Save).Text(Tx(TEXT("common.save"))).MinWidth(200.f).OnClick([Self]()
	{
		USmvDataService* D = Self->Data();
		if (!D) return;
		D->MutableSettings() = Self->EditSettings;
		FSmvI18n::SetLanguage(Self->EditSettings.Lang);
		ReducedMotion() = Self->EditSettings.bReducedMotion;
		if (Self->GameMode.IsValid()) if (USmvGameInstance* GI = USmvGameInstance::GetSmv(Self->GameMode.Get())) GI->ApplyGraphicsQuality(Self->EditSettings.GraphicsQuality);
		FSmvUiPrefs::Get().Save();
		if (Self->GameMode.IsValid()) Self->GameMode->ApplyUiPrefs();
		D->SaveSettings(FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->Refresh(); }));
	});
	return Page(Tx(TEXT("menu.settings")), Tx(TEXT("set.sub")), ESmvIcon::Gear,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[TabW]
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[SNew(SBox).HAlign(HAlign_Left)[SNew(SBox).WidthOverride(1000.f)[Body]]]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(Tx(TEXT("set.save_note")), 11, "Regular", Mute(), false)]
			+ SHorizontalBox::Slot().AutoWidth()[Save]
		], true, false, true);
}

TSharedRef<SWidget> SSmvShell::SetGeneral()
{
	SSmvShell* Self = this;
	FSmvSettings& S = EditSettings;
	FSmvUiPrefs& P = FSmvUiPrefs::Get();
	TArray<FText> LangL = { FText::FromString(TEXT("Türkçe")), FText::FromString(TEXT("English")), FText::FromString(TEXT("Deutsch")), FText::FromString(TEXT("Français")),
		FText::FromString(TEXT("Español")), FText::FromString(TEXT("Русский")), FText::FromString(TEXT("العربية")) };
	if (S.Lang.IsEmpty()) S.Lang = FSmvI18n::Language();
	auto SaveP = []() { FSmvUiPrefs::Get().Save(); };
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("settings.general")))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.lang")), Chips(&S.Lang, FSmvI18n::Languages(), LangL, {}, nullptr, 4))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.units")), Chips(&S.Units, { TEXT("metric"), TEXT("imperial") }, { Tx(TEXT("units.metric")), Tx(TEXT("units.imperial")) }, {}))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.gauge")), Chips(&S.GaugeTheme, { TEXT("military"), TEXT("fpv_osd"), TEXT("minimal") },
		{ Tx(TEXT("gauge.military")), Tx(TEXT("gauge.fpv_osd")), Tx(TEXT("gauge.minimal")) }, {}))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.analog_fpv")), Toggle(&S.bAnalogFpv))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.leader_hidden")), Toggle(&S.bLeaderboardHidden))];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 8)[Section(Tx(TEXT("set.interface")), Accent2())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.reduced_motion")), Toggle(&S.bReducedMotion, [Self]() { ReducedMotion() = Self->EditSettings.bReducedMotion; }), Tx(TEXT("set.reduced_motion_hint")))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.menu_scene")), Toggle(&P.bMenuScene, SaveP), Tx(TEXT("set.menu_scene_hint")))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.preview3d")), Toggle(&P.bPreview3d, SaveP))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.season")), Chips(&P.Season, { TEXT("auto"), TEXT("off") }, { Tx(TEXT("set.season_auto")), Tx(TEXT("set.season_off")) }, {}, SaveP))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("bind.cockpit")), Toggle(&P.bCockpit, SaveP), Tx(TEXT("set.cockpit_hint")))];
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[SNew(SSmvButton).Icon(ESmvIcon::Info).Text(Tx(TEXT("set.tutorial"))).OnClick([Self]() { Self->OpenTutorial(); })]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Icon(ESmvIcon::Plane).Text(Tx(TEXT("set.flight_tips"))).OnClick([Self]() { FSmvUiPrefs::Get().bFlightTipsDone = false; FSmvUiPrefs::Get().Save(); Self->Toast(FSmvI18n::T(TEXT("common.saved")).ToString(), 6); })]
	];
	return V;
}

TSharedRef<SWidget> SSmvShell::SetDisplay()
{
	SSmvShell* Self = this;
	FSmvSettings& S = EditSettings;
	FSmvDisplayEdit& D = GSmvDisp;
	// kalite ön ayarı kartları
	static const TCHAR* QKeys[] = { TEXT("q.low"), TEXT("q.medium"), TEXT("q.high"), TEXT("q.epic"), TEXT("q.cinematic") };
	TSharedRef<SUniformGridPanel> Q = SNew(SUniformGridPanel).SlotPadding(FMargin(5.f)).MinDesiredSlotWidth(160.f);
	for (int32 q = 0; q <= 4; ++q)
	{
		Q->AddSlot(q, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Tile).Icon(q < 2 ? ESmvIcon::Bolt : q < 4 ? ESmvIcon::Monitor : ESmvIcon::Camera).Selected(S.GraphicsQuality == q)
			.Text(FSmvI18n::T(QKeys[q])).SubText(FSmvI18n::T(FString(QKeys[q]) + TEXT("_sub")))
			.OnClick([Self, q]()
			{
				Self->EditSettings.GraphicsQuality = q;
				if (UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr) { G->SetOverallScalabilityLevel(q); G->ApplySettings(false); }
				GSmvDisp.Load();
				Self->Refresh();
			})
		];
	}
	TArray<FIntPoint> Res;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Res);
	TArray<FString> ResV; TArray<FText> ResL;
	for (const FIntPoint& R : Res) { const FString X = FString::Printf(TEXT("%dx%d"), R.X, R.Y); if (!ResV.Contains(X)) { ResV.Add(X); ResL.Add(FText::FromString(FString::Printf(TEXT("%d × %d"), R.X, R.Y))); } }
	if (!ResV.Contains(D.Res)) { ResV.Add(D.Res); ResL.Add(FText::FromString(D.Res)); }
	const TArray<FText> Lv = { Tx(TEXT("q.low")), Tx(TEXT("q.medium")), Tx(TEXT("q.high")), Tx(TEXT("q.epic")), Tx(TEXT("q.cinematic")) };
	auto LvFmt = [Lv](double V) { const int32 I = FMath::Clamp((int32)V, 0, 4); return Lv[I].ToString(); };
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("set.quality")))];
	V->AddSlot().AutoHeight()[Q];
	V->AddSlot().AutoHeight().Padding(5, 8, 0, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Bolt).Text(Tx(TEXT("set.auto_detect"))).OnClick([Self]()
	{
		if (UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr)
		{
			G->RunHardwareBenchmark();
			G->ApplyHardwareBenchmarkResults();
			Self->EditSettings.GraphicsQuality = FMath::Clamp(G->GetOverallScalabilityLevel(), 0, 4);
			GSmvDisp.Load();
			Self->Toast(FSmvI18n::T(TEXT("set.auto_done")).ToString(), 6);
			Self->Refresh();
		}
	})];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 8)[Section(Tx(TEXT("set.screen")), Accent2())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.resolution")), Cycle(&D.Res, ResV, ResL, nullptr, 260.f))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.window_mode")), Chips(&D.Mode, { TEXT("full"), TEXT("borderless"), TEXT("window") },
		{ Tx(TEXT("set.wm_full")), Tx(TEXT("set.wm_borderless")), Tx(TEXT("set.wm_window")) }, {}))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.vsync")), Toggle(&D.bVsync))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.fps_limit")), Chips(&D.Limit, { TEXT("30"), TEXT("60"), TEXT("120"), TEXT("144"), TEXT("0") },
		{ FText::FromString(TEXT("30")), FText::FromString(TEXT("60")), FText::FromString(TEXT("120")), FText::FromString(TEXT("144")), Tx(TEXT("set.unlimited")) }, {}))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.res_scale")), Slider(&D.Scale, 0, 1, 0.05, [](double X) { return FString::Printf(TEXT("%.0f%%"), 50.0 + X * 50.0); }))];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 8)[Section(Tx(TEXT("set.custom")), Warn())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.q_view")), SliderI(&D.View, 0, 4, 1, LvFmt))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.q_shadow")), SliderI(&D.Shadow, 0, 4, 1, LvFmt))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.q_texture")), SliderI(&D.Texture, 0, 4, 1, LvFmt))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.q_effects")), SliderI(&D.Effects, 0, 4, 1, LvFmt))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.q_post")), SliderI(&D.Post, 0, 4, 1, LvFmt))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.q_aa")), SliderI(&D.Aa, 0, 4, 1, LvFmt))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.q_foliage")), SliderI(&D.Foliage, 0, 4, 1, LvFmt))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.q_shading")), SliderI(&D.Shading, 0, 4, 1, LvFmt))];
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Btn(Tx(TEXT("set.apply_display")), [Self]()
	{
		UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!G) return;
		const FSmvDisplayEdit& X = GSmvDisp;
		FString A, B;
		if (X.Res.Split(TEXT("x"), &A, &B)) G->SetScreenResolution(FIntPoint(FCString::Atoi(*A), FCString::Atoi(*B)));
		G->SetFullscreenMode(X.Mode == TEXT("full") ? EWindowMode::Fullscreen : X.Mode == TEXT("borderless") ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		G->SetVSyncEnabled(X.bVsync);
		G->SetFrameRateLimit((float)FCString::Atoi(*X.Limit));
		G->SetResolutionScaleNormalized((float)X.Scale);
		G->SetViewDistanceQuality(X.View); G->SetShadowQuality(X.Shadow); G->SetTextureQuality(X.Texture); G->SetVisualEffectQuality(X.Effects);
		G->SetPostProcessingQuality(X.Post); G->SetAntiAliasingQuality(X.Aa); G->SetFoliageQuality(X.Foliage); G->SetShadingQuality(X.Shading);
		G->ApplySettings(false);
		G->SaveSettings();
		Self->Toast(FSmvI18n::T(TEXT("set.display_applied")).ToString(), 6);
	}, false, true, ESmvIcon::Check)];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 8)[Section(Tx(TEXT("settings.cesium")), Accent3())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.cesium")), TextEdit(&S.CesiumToken, true, Tx(TEXT("settings.cesium_hint")), 360.f))];
	return V;
}

TSharedRef<SWidget> SSmvShell::SetAudio()
{
	SSmvShell* Self = this;
	FSmvSettings& S = EditSettings;
	FSmvUiPrefs& P = FSmvUiPrefs::Get();
	auto Pct = [](double V) { return FString::Printf(TEXT("%.0f%%"), V * 100.0); };
	auto Live = [Self]()
	{
		// canlı önizleme: düzenlenen ana ses/aç-kapa geçici olarak uygulanır (Kaydet ile kalıcı)
		if (Self->Data()) { Self->Data()->MutableSettings().Volume = Self->EditSettings.Volume; Self->Data()->MutableSettings().bSound = Self->EditSettings.bSound; Self->Data()->MutableSettings().bMusic = Self->EditSettings.bMusic; }
		FSmvUiPrefs::Get().Save();
		if (Self->GameMode.IsValid()) Self->GameMode->ApplyUiPrefs();
	};
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("settings.audio")))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.sound")), Toggle(&S.bSound, Live))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.volume")), Slider(&S.Volume, 0, 1, 0.05, Pct, Live))];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 8)[Section(Tx(TEXT("set.mixer")), Accent2())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.music")), Toggle(&S.bMusic, Live))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.vol_music")), SliderF(&P.VolMusic, 0, 1, 0.05f, Pct, Live))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.vol_ui")), SliderF(&P.VolUi, 0, 1, 0.05f, Pct, [Live]() { Live(); FSmvUiSound::Play(ESmvSnd::Click); }))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.vol_alerts")), SliderF(&P.VolAlerts, 0, 1, 0.05f, Pct, Live))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.alert_tones")), Toggle(&S.bAlertTones))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.vol_motor")), SliderF(&P.VolMotor, 0, 1, 0.05f, Pct, Live))];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SmvUi::Text(Tx(TEXT("set.mixer_note")), 10, "Regular", Mute())];
	return V;
}

TSharedRef<SWidget> SSmvShell::SetControls()
{
	SSmvShell* Self = this;
	FSmvSettings& S = EditSettings;
	while (S.RcAxisMap.Num() < 5) S.RcAxisMap.Add(S.RcAxisMap.Num() + 1);
	while (S.RcInvert.Num() < 5) S.RcInvert.Add(false);
	static TArray<FString> AxisStore; AxisStore.SetNum(5);
	for (int32 i = 0; i < 5; ++i) AxisStore[i] = FString::FromInt(S.RcAxisMap[i]);
	TArray<FString> Axes = { TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8") };
	TArray<FText> AxesL = { Tx(TEXT("settings.axis_none")) };
	for (int32 i = 1; i <= 8; ++i) AxesL.Add(FText::FromString(FString::Printf(TEXT("Axis %d"), i)));
	TSharedRef<SUniformGridPanel> ModeGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(5.f)).MinDesiredSlotWidth(230.f);
	for (int32 m = 1; m <= 4; ++m)
	{
		TSharedRef<SSmvCanvas> Dia = SNew(SSmvCanvas).Size(FVector2D(220, 100));
		Dia->Painter = [m](const FGeometry& Gm, FSlateWindowElementList& Out, int32 L) { DrStickMode(Out, L, Gm, FVector2D::ZeroVector, FVector2D(Gm.GetLocalSize()), m, Accent()); };
		ModeGrid->AddSlot(m - 1, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Tile).Selected(S.StickMode == m).Text(FText::FromString(FString::Printf(TEXT("Mode %d"), m))).SubText(FSmvI18n::T(FString::Printf(TEXT("stickmode.m%d"), m)))
			.OnClick([Self, m]() { Self->EditSettings.StickMode = m; Self->EditSettings.bStickMode2 = m == 2; Self->Refresh(); })
			.Extra()[Dia]
		];
	}
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("stickmode.title")))];
	V->AddSlot().AutoHeight()[ModeGrid];
	V->AddSlot().AutoHeight().Padding(5, 8, 0, 4)[SmvUi::Text(FText::FromString(StickKeyHint(S.StickMode)), 13, "Bold", Accent(), false)];
	V->AddSlot().AutoHeight().Padding(5, 0, 0, 10)[SmvUi::Text(Tx(TEXT("stickmode.hint")), 10, "Regular", Mute())];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 8)[Section(Tx(TEXT("settings.input")), Accent2())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.device")), Chips(&S.InputDevice, { TEXT("keyboard"), TEXT("gamepad"), TEXT("rc") },
		{ Tx(TEXT("device.keyboard")), Tx(TEXT("device.gamepad")), Tx(TEXT("device.rc")) }, { ESmvIcon::Keyboard, ESmvIcon::Gamepad, ESmvIcon::Stick }, [Self]() { Self->Refresh(); }))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.expo")), Slider(&S.StickExpo, 0, 1, 0.05, [](double X) { return FString::Printf(TEXT("%.0f%%"), X * 100.0); }), Tx(TEXT("sens.expo_note")))];
	// hassasiyet (web §27): hazır ayar + özel (ölü bölge, expo, oran) + canlı eğri
	{
		FSmvUiPrefs& P = FSmvUiPrefs::Get();
		static FString SensP;
		SensP = P.SensPreset.IsEmpty() ? FString(TEXT("legacy")) : P.SensPreset;
		auto SaveP = []() { FSmvUiPrefs::Get().Save(); };
		auto Pct = [](double X) { return FString::Printf(TEXT("%.0f%%"), X * 100.0); };
		V->AddSlot().AutoHeight().Padding(0, 14, 0, 8)[Section(Tx(TEXT("sens.title")), Warn())];
		V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("sens.preset")), Chips(&SensP, { TEXT("legacy"), TEXT("precise"), TEXT("normal"), TEXT("agile"), TEXT("custom") },
			{ Tx(TEXT("sens.legacy")), Tx(TEXT("sens.precise")), Tx(TEXT("sens.normal")), Tx(TEXT("sens.agile")), Tx(TEXT("sens.custom")) }, {}, [Self]()
			{
				FSmvUiPrefs& Q = FSmvUiPrefs::Get();
				if (SensP == TEXT("legacy")) Q.SensPreset.Empty();
				else if (SensP == TEXT("custom")) Q.SensPreset = TEXT("custom");
				else Q.ApplySensPreset(SensP);
				Q.Save();
				Self->Refresh();
			}, 3))];
		if (P.SensPreset == TEXT("custom"))
		{
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("sens.deadzone")), SliderF(&P.Deadzone, 0.f, 0.2f, 0.01f, Pct, SaveP))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("sens.expo_rp")), SliderF(&P.ExpoRP, 0.f, 0.9f, 0.05f, Pct, SaveP))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("sens.expo_yaw")), SliderF(&P.ExpoYaw, 0.f, 0.9f, 0.05f, Pct, SaveP))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("sens.rate_rp")), SliderF(&P.RateRP, 0.3f, 1.f, 0.05f, Pct, SaveP))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("sens.rate_yaw")), SliderF(&P.RateYaw, 0.3f, 1.f, 0.05f, Pct, SaveP))];
		}
		TSharedRef<SSmvCanvas> Curve = SNew(SSmvCanvas).Size(FVector2D(260, 150));
		Curve->Painter = [Self](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			const FVector2D Sz = FVector2D(G.GetLocalSize());
			DrBox(Out, L, G, FVector2D::ZeroVector, Sz, Hex(0x0b1222, 0.9f), Round(10.f));
			DrSeg(Out, L + 1, G, FVector2D(0, Sz.Y * 0.5), FVector2D(Sz.X, Sz.Y * 0.5), Edge(), 1.f);
			DrSeg(Out, L + 1, G, FVector2D(Sz.X * 0.5, 0), FVector2D(Sz.X * 0.5, Sz.Y), Edge(), 1.f);
			DrSeg(Out, L + 1, G, FVector2D(0, Sz.Y), FVector2D(Sz.X, 0), FLinearColor(1, 1, 1, 0.12f), 1.f);
			const float Fe = (float)Self->EditSettings.StickExpo;
			for (int32 Ax : { 0, 3 })
			{
				TArray<FVector2D> P;
				for (int32 i = 0; i <= 40; ++i)
				{
					const float X = -1.f + 2.f * i / 40.f;
					const float Y = FSmvUiPrefs::Get().Shape(X, Ax, Fe);
					P.Add(FVector2D(Sz.X * (X + 1.f) * 0.5f, Sz.Y * (1.f - (Y + 1.f) * 0.5f)));
				}
				DrLine(Out, L + 2, G, P, Ax == 0 ? Accent() : Warn(), 2.f);
			}
			DrText(Out, L + 3, G, TEXT("roll/pitch"), FVector2D(8.f, 12.f), Font(9, "Bold"), Accent(), -1);
			DrText(Out, L + 3, G, TEXT("yaw"), FVector2D(8.f, 26.f), Font(9, "Bold"), Warn(), -1);
		};
		V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Row(Tx(TEXT("sens.curve")), Curve)];
	}
	if (S.InputDevice == TEXT("rc"))
	{
		const TCHAR* Slots[] = { TEXT("rc.roll"), TEXT("rc.pitch"), TEXT("rc.throttle"), TEXT("rc.yaw"), TEXT("rc.mode") };
		for (int32 i = 0; i < 5; ++i)
		{
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(Slots[i]), SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Cycle(&AxisStore[i], Axes, AxesL, [Self, i]() { Self->EditSettings.RcAxisMap[i] = FCString::Atoi(*AxisStore[i]); }, 200.f)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 8, 0)[SmvUi::Text(Tx(TEXT("settings.invert")), 11, "Regular", Dim(), false)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SSmvToggle)
					.Value_Lambda([Self, i]() { return Self->EditSettings.RcInvert[i]; })
					.OnChanged([Self, i](bool b) { Self->EditSettings.RcInvert[i] = b; })])];
		}
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(Tx(TEXT("settings.rc_note")), 10, "Regular", Mute())];
	}
	return V;
}

TSharedRef<SWidget> SSmvShell::SetKeys()
{
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[Section(Tx(TEXT("set.t_keys")))]
		+ SHorizontalBox::Slot().AutoWidth().Padding(14, 0, 0, 0)[SNew(SBox).WidthOverride(150.f)[SmvUi::Text(Tx(TEXT("set.key_kb")), 11, "Bold", Dim(), false)]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)[SNew(SBox).WidthOverride(150.f)[SmvUi::Text(Tx(TEXT("set.key_pad")), 11, "Bold", Dim(), false)]]
	];
	for (const FSmvBindDef& D : FSmvUiPrefs::Bindable())
	{
		const FName A = D.Action;
		auto OnKey = [Self, A](bool bPad)
		{
			return [Self, A, bPad](const FKey& K)
			{
				FSmvUiPrefs& P = FSmvUiPrefs::Get();
				const FName Other = P.ConflictOf(A, bPad, K);
				if (Other != NAME_None)
				{
					// çakışma: diğer eylemin tuşu bu eylemin eski tuşuyla değiştirilir
					const FKey Old = P.KeyFor(A, bPad);
					P.SetKey(Other, bPad, Old);
					Self->Toast(FSmvI18n::Format(TEXT("set.key_swapped"), { { TEXT("key"), SmvUi::KeyLabel(K) } }).ToString(), 4);
				}
				P.SetKey(A, bPad, K);
				P.Save();
				if (Self->GameMode.IsValid()) Self->GameMode->ApplyUiPrefs();
			};
		};
		V->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.025f)).Padding(FMargin(14.f, 5.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FSmvI18n::T(D.LabelKey), 13, "Regular", Fg(), false)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(14, 0, 0, 0)[SNew(SSmvKeyCapture).bPad(false).Current_Lambda([A]() { return SmvUi::KeyLabel(FSmvUiPrefs::Get().KeyFor(A, false)); }).OnCaptured(OnKey(false))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)[SNew(SSmvKeyCapture).bPad(true).Current_Lambda([A]() { return SmvUi::KeyLabel(FSmvUiPrefs::Get().KeyFor(A, true)); }).OnCaptured(OnKey(true))]
			]
		];
	}
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[SNew(SSmvButton).Icon(ESmvIcon::Replay).Text(Tx(TEXT("set.key_reset"))).OnClick([Self]()
		{
			FSmvUiPrefs::Get().ResetKeys(); FSmvUiPrefs::Get().Save();
			if (Self->GameMode.IsValid()) Self->GameMode->ApplyUiPrefs();
			Self->Refresh();
		})]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(Tx(TEXT("set.key_note")), 10, "Regular", Mute())]
	];
	return V;
}

TSharedRef<SWidget> SSmvShell::SetAlerts()
{
	FSmvSettings& S = EditSettings;
	auto Deg = [](double V) { return FString::Printf(TEXT("%.0f°"), V); };
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("gpws.title")), Danger())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("gpws.mode")), Chips(&S.GpwsMode, { TEXT("off"), TEXT("visual"), TEXT("full") }, { Tx(TEXT("gpws.mode_off")), Tx(TEXT("gpws.mode_visual")), Tx(TEXT("gpws.mode_full")) }, {}))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("gpws.sens")), Chips(&S.GpwsSens, { TEXT("low"), TEXT("normal"), TEXT("high") }, { Tx(TEXT("gpws.sens_low")), Tx(TEXT("gpws.sens_normal")), Tx(TEXT("gpws.sens_high")) }, {}))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("gpws.lang")), Chips(&S.GpwsLang, { TEXT("aviation"), TEXT("ui") }, { Tx(TEXT("gpws.lang_aviation")), Tx(TEXT("gpws.lang_ui")) }, {}))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("gpws.bank_copter")), SliderI(&S.BankCopter, 20, 60, 1, Deg))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("gpws.bank_plane")), SliderI(&S.BankPlane, 30, 70, 1, Deg))];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Volume).Text(Tx(TEXT("set.alert_test"))).OnClick([]() { FSmvUiSound::Play(ESmvSnd::Error); })];
	return V;
}

TSharedRef<SWidget> SSmvShell::SetLink()
{
	FSmvSettings& S = EditSettings;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("settings.mp")), Accent3())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.mp_out")), Toggle(&S.bMpUdpOut))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.mp_host")), TextEdit(&S.MpUdpHost, false, FText::GetEmpty(), 240.f))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("settings.mp_port")), SliderI(&S.MpUdpPort, 1024, 65535, 1))];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SmvUi::Text(Tx(TEXT("settings.mp_note")), 10, "Regular", Mute())];
	return V;
}

#undef LOCTEXT_NAMESPACE
