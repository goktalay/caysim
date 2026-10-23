#include "SSmvShell.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SmvDataService.h"
#include "SmvFlightLink.h"
#include "SmvLocalFlightLink.h"
#include "SmvMatchClient.h"
#include "SmvGameMode.h"
#include "SmvPilotPawn.h"
#include "SmvGameInstance.h"
#include "SmvSteam.h"
#include "SmvFabContent.h"
#include "SmvMenuScene.h"
#include "SmvUiPrefs.h"
#include "SmvFpv.h"
#include "SmvI18n.h"
#include "SmvSpeech.h"
#include "SmvReplay.h"
#include "SmvTileMap.h"
#include "SmvHoliday.h"
#include "HAL/PlatformProcess.h"
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
#include "Widgets/Text/SMultiLineEditableText.h"
#include "UnrealClient.h"
#include "Misc/Base64.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Engine/GameViewportClient.h"

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

FString SSmvShell::SeasonId() { return SmvHoliday::ActiveTheme(); }

FLinearColor SSmvShell::SeasonColor(const FString& Id) { return SmvHoliday::ThemeColor(Id); }

FString SSmvShell::HolidayLine()
{
	FSmvHolidayItem I;
	SmvHoliday::ActiveTheme(&I);
	return I.Name + (I.Country.IsEmpty() ? FString() : TEXT(" · ") + I.Country);
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
	if (Data()) { EditSettings = Data()->Settings(); ReducedMotion() = EditSettings.bReducedMotion; }

	// açılış logosu (bir kez)
	{
		static bool bLogoShown = false;
		if (!bLogoShown && !ReducedMotion())
		{
			bLogoShown = true;
			LogoAt = FPlatformTime::Seconds();
			TArray<uint8> Png;
			if (FFileHelper::LoadFileToArray(Png, *(FPaths::ProjectContentDir() / TEXT("Simumav/Brand/icon-512.png"))))
				if (UTexture2D* Tx2 = FImageUtils::ImportBufferAsTexture2D(Png))
				{
					LogoTex.Reset(Tx2);
					LogoBrush.DrawAs = ESlateBrushDrawType::Image;
					LogoBrush.SetResourceObject(Tx2);
					LogoBrush.ImageSize = FVector2D(512, 512);
				}
			FSmvUiSound::Play(ESmvSnd::Open);
		}
	}
	TSharedRef<SSmvCanvas> Logo = SNew(SSmvCanvas);
	Logo->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() { return FPlatformTime::Seconds() - LogoAt < 3.6 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }));
	Logo->Painter = [this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const double T = FPlatformTime::Seconds() - LogoAt;
		if (T > 3.6) return;
		const FVector2D Sz = FVector2D(G.GetLocalSize()), C = Sz * 0.5;
		const float Fade = (float)FMath::Clamp((3.6 - T) / 0.8, 0.0, 1.0);
		DrBox(Out, L, G, FVector2D::ZeroVector, Sz, FLinearColor(0.01f, 0.015f, 0.03f, Fade), nullptr);
		// ışık halkası + simge (ölçeklenip döner)
		const float In = (float)FMath::Clamp(T / 1.1, 0.0, 1.0), E = 1.f - FMath::Pow(1.f - In, 3.f);
		const float Ls = 150.f * (0.7f + 0.3f * E);
		DrGlow(Out, L + 1, G, C - FVector2D(0, 40), Ls * 1.8f, Accent().CopyWithNewOpacity(0.35f * E * Fade));
		for (int32 k = 0; k < 3; ++k) DrArc(Out, L + 2, G, C - FVector2D(0, 40), Ls * (0.75f + k * 0.12f), (float)(T * (90 + k * 40)), (float)(T * (90 + k * 40)) + 120.f + k * 60.f, Accent().CopyWithNewOpacity(0.7f * Fade * E), 2.f);
		if (LogoTex.IsValid())
			FSlateDrawElement::MakeBox(Out, L + 3, G.ToPaintGeometry(FVector2f(Ls, Ls), FSlateLayoutTransform(FVector2f(C - FVector2D(Ls * 0.5, 40 + Ls * 0.5)))), &LogoBrush, ESlateDrawEffect::None, FLinearColor(1, 1, 1, E * Fade));
		// kelime işareti: harf harf belirir + ışık süpürmesi
		const FString Word = TEXT("SIMUMAV");
		const FSlateFontInfo F = SmvUi::Font(54, "Black");
		const float Step = 46.f, X0 = (float)C.X - Step * (Word.Len() - 1) * 0.5f;
		for (int32 i = 0; i < Word.Len(); ++i)
		{
			const float A = (float)FMath::Clamp((T - 0.7 - i * 0.07) / 0.35, 0.0, 1.0);
			DrText(Out, L + 4, G, Word.Mid(i, 1), FVector2D(X0 + i * Step, C.Y + 120 + (1.f - A) * 18.f), F, FLinearColor(1, 1, 1, A * Fade), 0);
		}
		const float Sx = (float)FMath::Lerp(-0.2, 1.2, FMath::Clamp((T - 1.4) / 0.9, 0.0, 1.0)) * (float)Sz.X;
		DrGlow(Out, L + 5, G, FVector2D(Sx, C.Y + 120), 60.f, FLinearColor(1, 1, 1, 0.35f * Fade));
		const float Sub = (float)FMath::Clamp((T - 1.6) / 0.5, 0.0, 1.0);
		DrText(Out, L + 5, G, TEXT("CAYSIM · ") + FSmvI18n::T(TEXT("menu.fly_sub")).ToString(), FVector2D(C.X, C.Y + 176), SmvUi::Font(14, "Regular"), Dim().CopyWithNewOpacity(Sub * Fade), 0);
	};
	TSharedRef<SSmvCanvas> Sweep = SNew(SSmvCanvas);
	Sweep->SetVisibility(EVisibility::HitTestInvisible);
	Sweep->Painter = [this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		if (ReducedMotion()) return;
		const float K = (float)(FPlatformTime::Seconds() - TransAt) / 0.55f;
		if (K < 0.f || K > 1.f) return;
		const FVector2D Sz = G.GetLocalSize();
		const float X = FMath::Lerp(-0.3f, 1.3f, K) * (float)Sz.X;
		const float A = FMath::Sin(K * PI) * 0.10f;
		const FLinearColor C1 = Accent().CopyWithNewOpacity(A), C2 = FLinearColor(1, 1, 1, A * 0.9f);
		TArray<FVector2D> P;
		for (int32 i = 0; i < 3; ++i)
		{
			const float W = 70.f + i * 60.f;
			P = { FVector2D(X - W, Sz.Y), FVector2D(X - W + Sz.Y * 0.35f, 0.f) };
			FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, i == 0 ? C2 : C1, true, W * 0.5f);
		}
	};
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
				.Tint_Lambda([]() { return SmvUi::ReducedMotion() ? FLinearColor::Transparent : SeasonColor(SeasonId()); })
				.Scene_Lambda([this]()
				{
					// bölüme özel arka plan (web bölüm arka planları): canlı 3B sahne varken menüde gizli kalır
					switch (Built)
					{
					case ESmvScreen::Hangar: case ESmvScreen::Crew: return 1;
					case ESmvScreen::Missions: case ESmvScreen::Wizard: case ESmvScreen::Tracks: case ESmvScreen::Loading: return 2;
					case ESmvScreen::Lobby: case ESmvScreen::Leaderboard: case ESmvScreen::Account: return 3;
					case ESmvScreen::Logbook: case ESmvScreen::Summary: case ESmvScreen::Replay: return 4;
					case ESmvScreen::FlyMode: case ESmvScreen::FpvGallery: case ESmvScreen::Settings: case ESmvScreen::Support: return 5;
					default: return 0;
					}
				})
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0.f)
			.ColorAndOpacity_Lambda([this]()
			{
				if (ReducedMotion()) return FLinearColor::White;
				const float K = FMath::Clamp((float)(FPlatformTime::Seconds() - TransAt) / 0.32f, 0.f, 1.f);
				return FLinearColor(1, 1, 1, 1.f - (1.f - K) * (1.f - K));
			})
			.RenderTransform_Lambda([this]()
			{
				const float K = ReducedMotion() ? 1.f : FMath::Clamp((float)(FPlatformTime::Seconds() - TransAt) / 0.38f, 0.f, 1.f);
				const float E = 1.f - FMath::Pow(1.f - K, 3.f);
				return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f((1.f - E) * 46.f * TransDir, (1.f - E) * 10.f)));
			})
			[
				SAssignNew(Content, SBox)
			]
		]
		// ışık süpürmesi (geçişte soldan sağa çapraz parlak şerit)
		+ SOverlay::Slot()
		[
			Sweep
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
		+ SOverlay::Slot()
		[
			Logo
		]
	];
	// kabuk tüm ekranı kaplar: kendisi tıklamayı yutmasın (alttaki uçuş arayüzü düğmeleri çalışsın)
	SetVisibility(EVisibility::SelfHitTestInvisible);
	Refresh();
}

void SSmvShell::Refresh() { bDirty = true; }

void SSmvShell::Confirm(const FText& Title, const FText& Body, const FText& YesLabel, bool bDanger, TFunction<void()> OnYes)
{
	if (!OverlayBox.IsValid()) return;
	TSharedRef<SSmvButton> No = SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Back).MinWidth(150.f).Text(FSmvI18n::T(TEXT("common.cancel")))
		.OnClick([this]() { CloseOverlay(); });
	const double At = FPlatformTime::Seconds();
	OverlayBox->SetContent(
		SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(0.f)
		.BorderBackgroundColor_Lambda([At]() { return FSlateColor(FLinearColor(0.f, 0.f, 0.02f, 0.6f * FMath::Clamp((float)(FPlatformTime::Seconds() - At) * 6.f, 0.f, 1.f))); })
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(500.f)
			.RenderTransform_Lambda([At]()
			{
				const float K = ReducedMotion() ? 1.f : FMath::Clamp((float)(FPlatformTime::Seconds() - At) * 5.f, 0.f, 1.f);
				const float S = 0.92f + 0.08f * (1.f - (1.f - K) * (1.f - K));
				return TOptional<FSlateRenderTransform>(FSlateRenderTransform(S));
			})
			.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			[
				SNew(SBorder).BorderImage(GlassCard()).Padding(FMargin(26.f, 22.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Icon(bDanger ? ESmvIcon::Trash : ESmvIcon::Help, 26.f, bDanger ? Danger() : Accent())]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Title.IsEmpty() ? SNullWidget::NullWidget : SmvUi::Title(Title, 20)]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 20)[SmvUi::Text(Body, 13, "Regular", Dim())]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[No]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SSmvButton).Kind(bDanger ? ESmvBtn::Danger : ESmvBtn::Primary).MinWidth(150.f).Text(YesLabel)
							.Icon(bDanger ? ESmvIcon::Trash : ESmvIcon::Check)
							.OnClick([this, OnYes]() { CloseOverlay(); if (OnYes) OnYes(); })
						]
					]
				]
			]
		]);
	bOverlay = true;
	FSmvUiSound::Play(ESmvSnd::Open);
	DefaultFocus = No;
	bWantFocus = true;
}

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
			if (Built == ESmvScreen::Support) { Sup.OpenId.Empty(); Sup.Detail.Reset(); SupLoad(); }
			if (Built == ESmvScreen::Replay) { RepList = SmvReplay::List(); RepLoaded.Empty(); RepSelect(0); }
			if (Built == ESmvScreen::Wizard) WizStep = 0;
			FSmvUiSound::Play(ESmvSnd::Open);
		}
		DefaultFocus.Reset();
		TabStep = nullptr;
		bPreviewVisible = false;
		// Arapça: sağdan sola yerleşim (yatay kutular, hizalamalar aynalanır)
		Content->SetContent(SNew(SBox).FlowDirectionPreference(FSmvI18n::IsRtl() ? EFlowDirectionPreference::RightToLeft : EFlowDirectionPreference::LeftToRight)
			[
				BuildScreen(Built)
			]);
		if (Built == ESmvScreen::Summary) SummaryAt = FPlatformTime::Seconds();
		if (bScreenChanged) { TransAt = FPlatformTime::Seconds(); TransDir = (Built == ESmvScreen::Menu) ? -1 : 1; }
		bWantFocus = true;
		// ilk açılış tanıtımı (ana menüye ilk girişte)
		if (Built == ESmvScreen::Menu && !FSmvUiPrefs::Get().bTutorialDone && !bOverlay) OpenTutorial();
	}
	// dünya oluşturucu canlı önizlemesi: parametre değişince 0,6 sn sonra yeniden kur
	if (Built == ESmvScreen::Tracks && MapMode == TEXT("3d"))
	{
		const FSmvTrackWorld& Wo = EditTrack.World;
		const uint32 H = HashCombine(HashCombine(GetTypeHash(Wo.Seed), GetTypeHash(Wo.MountainCount)), HashCombine(GetTypeHash((int32)(Wo.TreeDensity * 100)), HashCombine(GetTypeHash(Wo.BuildingCount),
			HashCombine(GetTypeHash((int32)(Wo.Water * 100)), HashCombine(GetTypeHash((int32)Wo.MountainHeight), HashCombine(GetTypeHash((int32)Wo.SizeM), GetTypeHash(Wo.bRunway)))))));
		if (H != WorldPrevHash) { WorldPrevHash = H; WorldPrevAt = FPlatformTime::Seconds(); bWorldPrevDirty = true; }
		if (bWorldPrevDirty && FPlatformTime::Seconds() - WorldPrevAt > 0.6) { bWorldPrevDirty = false; if (ASmvMenuScene* S = Scene()) S->SetWorldPreview(EditTrack); }
	}
	if (ASmvMenuScene* S = Scene()) S->SetPreviewActive(bPreviewVisible);
	// RC eksen algılama: tabandan en çok sapan eksen (> 0,45) seçilir; eksi yönde ise ters çevrilir (kol yukarı / sağa itilir)
	if (DetectSlot >= 0)
	{
		ASmvPilotPawn* Pw = GameMode.IsValid() ? GameMode->GetPilot() : nullptr;
		int32 Best = -1; float BestD = 0.45f, BestSigned = 0.f;
		for (int32 k = 0; Pw && k < 8; ++k)
		{
			const float D = Pw->GetRawAxis(k) - DetectBase[k];
			if (FMath::Abs(D) > BestD) { BestD = FMath::Abs(D); Best = k; BestSigned = D; }
		}
		if (Best >= 0 && EditSettings.RcAxisMap.IsValidIndex(DetectSlot))
		{
			static const TCHAR* SlotKey[] = { TEXT("rc.roll"), TEXT("rc.pitch"), TEXT("rc.throttle"), TEXT("rc.yaw"), TEXT("rc.mode") };
			EditSettings.RcAxisMap[DetectSlot] = Best + 1;
			if (EditSettings.RcInvert.IsValidIndex(DetectSlot)) EditSettings.RcInvert[DetectSlot] = BestSigned < 0.f;
			Toast(FSmvI18n::Format(TEXT("pad.learned"), { { TEXT("a"), FSmvI18n::T(SlotKey[FMath::Clamp(DetectSlot, 0, 4)]).ToString() }, { TEXT("n"), FString::FromInt(Best + 1) },
				{ TEXT("inv"), BestSigned < 0.f ? FString(TEXT(" (") ) + FSmvI18n::T(TEXT("pad.inv")).ToString() + TEXT(")") : FString() } }).ToString(), 6);
			DetectSlot = -1;
			Refresh();
		}
		else if (FPlatformTime::Seconds() - DetectAt > 8.0) { Toast(FSmvI18n::T(TEXT("pad.learn_timeout")).ToString(), 4); DetectSlot = -1; }
	}
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
	if (FPlatformTime::Seconds() - LogoAt < 3.6) { LogoAt = FPlatformTime::Seconds() - 2.9; return FReply::Handled(); }
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
	case ESmvScreen::FlyMode: return FlyMode();
	case ESmvScreen::FpvGallery: return FpvGallery();
	case ESmvScreen::Replay: return ReplayScreen();
	case ESmvScreen::Support: return Support();
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
	B->AddSlot().AutoHeight().Padding(0, 18, 0, 0)[bIn ? Btn(Tx(TEXT("account.logout")), [this, F]() { Confirm(FSmvI18n::T(TEXT("dlg.logout_title")), FSmvI18n::T(TEXT("dlg.logout_body")), FSmvI18n::T(TEXT("account.logout")), false, [F]() { if (F.IsValid()) F->Logout(); }); }, false, true, ESmvIcon::Power)
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
		.OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::FlyMode); });
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
	Sm(Tx(TEXT("support.help")), ESmvIcon::Info, Go(ESmvScreen::Support));
	Sm(Tx(TEXT("menu.quit")), ESmvIcon::Power, [Self = this, G = GameMode]()
	{
		Self->Confirm(FSmvI18n::T(TEXT("dlg.quit_title")), FSmvI18n::T(TEXT("dlg.quit_body")), FSmvI18n::T(TEXT("dlg.yes_quit")), true,
			[G]() { if (G.IsValid()) UKismetSystemLibrary::QuitGame(G.Get(), nullptr, EQuitPreference::Quit, false); });
	}, true);

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
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FSmvI18n::T(TEXT("holiday.greet_") + Season), 14, "Bold", FLinearColor::White)]
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(HolidayLine()), 11, "Regular", FLinearColor(1, 1, 1, 0.8f))]
				]
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
			const bool bWasNew = Self->EditDrone.Id.IsEmpty();
			Self->Data()->SaveDrone(Self->EditDrone, FSmvDone::CreateLambda([Self, bWasNew](bool bOk, const FString& M)
			{
				if (bOk && bWasNew)
				{
					// "hangara indi": yeni dron önizlemede yukarıdan süzülerek iner
					if (Self->Data()->Drones().Num()) Self->EditDrone = Self->Data()->Drones().Last();
					if (ASmvMenuScene* Sc = Self->Scene()) Sc->PlayLanding();
					Self->Toast(Self->EditDrone.Name + TEXT(" — ") + FSmvI18n::T(TEXT("hangar.landed")).ToString(), 6);
				}
				else Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3);
				Self->Refresh();
			}));
		}, true, true, ESmvIcon::Save)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.make_default")), [Self]() { Self->Data()->SetDefault(TEXT("drone"), Self->EditDrone.Id); Self->EditDrone.bIsDefault = true; Self->Refresh(); }, false,
			TAttribute<bool>(!bNew), ESmvIcon::Star)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Trash).Text(Tx(TEXT("common.delete"))).IsEnabled(!bNew).OnClick([Self]()
		{
			Self->Confirm(FSmvI18n::T(TEXT("dlg.delete_title")), FSmvI18n::Format(TEXT("dlg.delete_body"), { { TEXT("n"), Self->EditDrone.Name } }), FSmvI18n::T(TEXT("dlg.yes_delete")), true, [Self]()
			{
				Self->Data()->DeleteDrone(Self->EditDrone.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); if (bOk && Self->Data()->Drones().Num()) Self->EditDrone = Self->Data()->Drones()[0]; Self->Refresh(); }));
			});
		})]
	];
	// dron şeridi: mini 3B kartlar (menü sahnesinde çekilen anlık görüntüler; ★ varsayılan)
	TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
	for (const FSmvDrone& X : D->Drones())
	{
		const FSmvDrone Copy = X;
		const FLinearColor Ac = SmvHex(X.Color, Accent());
		UTextureRenderTarget2D* Rt = (Scene() && FSmvUiPrefs::Get().bPreview3d) ? Scene()->DroneSnap(X.Type, X.ModelKey, X.Color) : nullptr;
		TSharedRef<SWidget> Img = SNullWidget::NullWidget;
		if (Rt)
		{
			TSharedPtr<FSlateBrush>& Br = AvatarBrushes.FindOrAdd(Rt->GetName());
			if (!Br.IsValid()) { Br = MakeShared<FSlateBrush>(); Br->DrawAs = ESlateBrushDrawType::Image; Br->SetResourceObject(Rt); Br->ImageSize = FVector2D(144, 90); }
			Img = SNew(SImage).Image(Br.Get());
		}
		else Img = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[Icon(IconForType(X.Type), 40.f, Ac)];
		Strip->AddSlot().AutoWidth().Padding(0, 0, 10, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Ac).MinWidth(160.f).Selected(X.Id == EditDrone.Id).OnClick([Self, Copy]() { Self->EditDrone = Copy; Self->Refresh(); })
			.Extra()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(144.f).HeightOverride(90.f)[Img]]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[SmvUi::Text(FText::FromString((X.bIsDefault ? FString(TEXT("★ ")) : FString()) + X.Name), 12, "Bold", Fg(), false)]
			]
		];
	}
	return Page(Tx(TEXT("menu.hangar")), Tx(TEXT("menu.hangar_sub")), ESmvIcon::Drone, SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)[SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarVisibility(EVisibility::Collapsed) + SScrollBox::Slot()[Strip]]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[SNew(SBox).WidthOverride(320.f)[List]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[Mid]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Ed, FMargin(20.f))]]);
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
	const bool bNew = EditPilot.Id.IsEmpty();
	auto Re = [Self]() { Self->Refresh(); };
	FSmvAvatar& A = EditPilot.Avatar;
	auto En = [](const TCHAR* Field, std::initializer_list<const TCHAR*> Vals, TArray<FString>& V, TArray<FText>& L)
	{
		for (const TCHAR* X : Vals) { V.Add(X); L.Add(FSmvI18n::T(FString::Printf(TEXT("avatar.%s_%s"), Field, X))); }
	};
	auto Pick = [this, Re, En](FString* Target, const TCHAR* Field, std::initializer_list<const TCHAR*> Vals, int32 PerRow) -> TSharedRef<SWidget>
	{
		TArray<FString> V; TArray<FText> L;
		En(Field, Vals, V, L);
		return Row(FSmvI18n::T(FString(TEXT("avatar.")) + Field), Chips(Target, V, L, {}, Re, PerRow));
	};
	// 3B önizleme (sürükle: döndür) ya da çizilmiş portre
	ASmvMenuScene* Sc = Scene();
	const bool b3d = Sc && FSmvUiPrefs::Get().bPreview3d && Sc->GetPreviewTarget();
	TSharedRef<SWidget> Prev = SNullWidget::NullWidget;
	if (b3d)
	{
		Sc->SetPilotPreview(A, bFaceZoom);
		PreviewBrush.SetResourceObject(Sc->GetPreviewTarget());
		PreviewBrush.ImageSize = FVector2D(960, 600);
		bPreviewVisible = true;
		TSharedRef<SSmvCanvas> Drag = SNew(SSmvCanvas).Size(FVector2D(400, 460));
		TSharedPtr<FVector2D> LastP = MakeShared<FVector2D>(FVector2D::ZeroVector);
		TWeakObjectPtr<ASmvMenuScene> WS(Sc);
		Drag->OnDrag = [WS, LastP](const FGeometry&, const FVector2D& P, int32 Phase)
		{
			if (Phase == 0) { *LastP = P; return; }
			if (Phase == 1 && WS.IsValid()) { WS->AddPreviewYaw((float)(P.X - LastP->X) * 0.6f); *LastP = P; }
		};
		Prev = SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SImage).Image(&PreviewBrush)]
			+ SOverlay::Slot()[Drag];
	}
	else Prev = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[Portrait(&A, 300.f)];
	TSharedRef<SVerticalBox> Mid = SNew(SVerticalBox);
	Mid->AddSlot().AutoHeight()[SNew(SBox).WidthOverride(400.f).HeightOverride(460.f)[SNew(SBorder).BorderImage(GlassCard()).Padding(0)[Prev]]];
	Mid->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(12).Selected(!bFaceZoom).Text(Tx(TEXT("avatar.view_body"))).OnClick([Self]() { Self->bFaceZoom = false; Self->Refresh(); })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(12).Selected(bFaceZoom).Text(Tx(TEXT("avatar.view_face"))).OnClick([Self]() { Self->bFaceZoom = true; Self->Refresh(); })]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(Tx(TEXT("avatar.drag")), 10, "Regular", Mute(), false)]
	];
	Mid->AddSlot().AutoHeight().Padding(4, 10, 0, 0)[Title(FText::FromString(EditPilot.Name.IsEmpty() ? FString(TEXT("—")) : EditPilot.Name), 22)];
	if (!bNew) Mid->AddSlot().AutoHeight().Padding(4, 4, 0, 0)[SmvUi::Text(FSmvI18n::Format(TEXT("crew.stats"), { { TEXT("n"), FString::FromInt(EditPilot.Flights) }, { TEXT("h"), FString::Printf(TEXT("%.1f"), EditPilot.Hours) } }), 12, "Regular", Dim(), false)];
	Mid->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("avatar.random")), [Self]()
		{
			FSmvAvatar& Av = Self->EditPilot.Avatar;
			auto R = [](std::initializer_list<const TCHAR*> L) { const int32 N = (int32)L.size(); return FString(*(L.begin() + FMath::RandRange(0, N - 1))); };
			const TArray<FString>& Pal = SmvPalette();
			auto C = [&Pal]() { return Pal[FMath::RandRange(0, Pal.Num() - 1)]; };
			if (!Self->bKeepBody) Av.Body = R({ TEXT("m"), TEXT("f") });
			Av.Build = R({ TEXT("slim"), TEXT("avg"), TEXT("broad") }); Av.Face = R({ TEXT("oval"), TEXT("round"), TEXT("square"), TEXT("long"), TEXT("heart") });
			Av.Eyes = R({ TEXT("almond"), TEXT("round"), TEXT("narrow"), TEXT("wide") }); Av.Brows = R({ TEXT("straight"), TEXT("arched"), TEXT("angled"), TEXT("soft") });
			Av.BrowW = R({ TEXT("thin"), TEXT("medium"), TEXT("thick") }); Av.Nose = R({ TEXT("small"), TEXT("straight"), TEXT("wide"), TEXT("pointed") });
			Av.Mouth = R({ TEXT("neutral"), TEXT("smile"), TEXT("grin"), TEXT("serious") });
			Av.Beard = Av.Body == TEXT("f") ? FString(TEXT("none")) : R({ TEXT("none"), TEXT("none"), TEXT("stubble"), TEXT("mustache"), TEXT("goatee"), TEXT("full") });
			Av.Hair = Av.Body == TEXT("f") ? R({ TEXT("long"), TEXT("ponytail"), TEXT("bun"), TEXT("bob"), TEXT("curly"), TEXT("short") }) : R({ TEXT("short"), TEXT("buzz"), TEXT("spiky"), TEXT("sidepart"), TEXT("curly"), TEXT("mohawk"), TEXT("bald") });
			Av.HairColor = R({ TEXT("#1c1410"), TEXT("#3b2a1e"), TEXT("#5a3b22"), TEXT("#8a5a2b"), TEXT("#c48a3f"), TEXT("#e1c27a"), TEXT("#9a9a9a"), TEXT("#b23a2e") });
			Av.EyeColor = R({ TEXT("#5b3a1e"), TEXT("#2f5d8a"), TEXT("#3f7a33"), TEXT("#6b4f2a"), TEXT("#444444") });
			Av.Glasses = R({ TEXT("none"), TEXT("none"), TEXT("none"), TEXT("aviator"), TEXT("sun") });
			Av.Headgear = R({ TEXT("helmet"), TEXT("helmet"), TEXT("none"), TEXT("cap"), TEXT("headset") });
			Av.Outfit = R({ TEXT("suit"), TEXT("jacket"), TEXT("casual") });
			Av.bFreckles = FMath::RandRange(0, 5) == 0; Av.bMole = FMath::RandRange(0, 6) == 0; Av.bVisorOpen = FMath::RandBool();
			Av.Helmet = C(); Av.Suit = C(); Av.Visor = C(); Av.PatchColor = C(); Av.bMirror = FMath::RandBool();
			Av.Skin = FString::Printf(TEXT("s%d"), FMath::RandRange(1, 8));
			Av.Patch = R({ TEXT("none"), TEXT("star"), TEXT("wing"), TEXT("bolt"), TEXT("chevron"), TEXT("target"), TEXT("shield"), TEXT("cross") });
			Av.Accessory = R({ TEXT("none"), TEXT("mic"), TEXT("crest"), TEXT("antenna"), TEXT("stripe") });
			Self->Refresh();
		}, false, true, ESmvIcon::Star)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Toggle(&bKeepBody)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(Tx(TEXT("avatar.keep_body")), 11, "Regular", Dim(), false)]
	];
	// hazır pilotlar (eski 2B karakterlerin 3B karşılığı — sunucu LEGACY tablosu)
	{
		struct FLeg { const TCHAR* H; const TCHAR* S; const TCHAR* Sk; const TCHAR* V; bool bM; const TCHAR* P; const TCHAR* Pc; const TCHAR* Ac; };
		static const FLeg Leg[10] = {
			{ TEXT("#1e3a5f"), TEXT("#1e3a5f"), TEXT("s4"), TEXT("#f5c542"), true, TEXT("wing"), TEXT("#f5c542"), TEXT("mic") },
			{ TEXT("#f8fafc"), TEXT("#7c3aed"), TEXT("s3"), TEXT("#f08080"), false, TEXT("bolt"), TEXT("#f5c542"), TEXT("none") },
			{ TEXT("#111111"), TEXT("#0f766e"), TEXT("s8"), TEXT("#9fd3ff"), true, TEXT("chevron"), TEXT("#9fd3ff"), TEXT("stripe") },
			{ TEXT("#b91c1c"), TEXT("#b91c1c"), TEXT("s6"), TEXT("#1a1a1a"), true, TEXT("star"), TEXT("#f5c542"), TEXT("crest") },
			{ TEXT("#d9a441"), TEXT("#374151"), TEXT("s2"), TEXT("#374151"), true, TEXT("wing"), TEXT("#d9a441"), TEXT("mic") },
			{ TEXT("#0369a1"), TEXT("#0369a1"), TEXT("s7"), TEXT("#f08080"), false, TEXT("target"), TEXT("#f5c542"), TEXT("antenna") },
			{ TEXT("#8c8c8c"), TEXT("#4d7c0f"), TEXT("s1"), TEXT("#9fd3ff"), false, TEXT("shield"), TEXT("#f5c542"), TEXT("none") },
			{ TEXT("#f0f0f0"), TEXT("#be185d"), TEXT("s2"), TEXT("#f08080"), true, TEXT("star"), TEXT("#f0f0f0"), TEXT("mic") },
			{ TEXT("#ea580c"), TEXT("#ea580c"), TEXT("s6"), TEXT("#1a1a1a"), true, TEXT("cross"), TEXT("#ffffff"), TEXT("stripe") },
			{ TEXT("#0e7490"), TEXT("#0e7490"), TEXT("s5"), TEXT("#f5c542"), true, TEXT("chevron"), TEXT("#f5c542"), TEXT("mic") } };
		TSharedRef<SHorizontalBox> Pr = SNew(SHorizontalBox);
		for (int32 k = 0; k < 10; ++k)
		{
			const FLeg L = Leg[k];
			Pr->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SSmvButton).Kind(ESmvBtn::Ghost).MinHeight(34.f).OnClick([Self, L, k]()
				{
					FSmvAvatar& Av = Self->EditPilot.Avatar;
					Av.Helmet = L.H; Av.Suit = L.S; Av.Skin = L.Sk; Av.Visor = L.V; Av.bMirror = L.bM; Av.Patch = L.P; Av.PatchColor = L.Pc; Av.Accessory = L.Ac;
					Self->EditPilot.Character = FString::Printf(TEXT("p%02d"), k + 1);
					Self->Refresh();
				})
				.Extra()[SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(SmvHex(L.H))]]
			];
		}
		Mid->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[SmvUi::Text(Tx(TEXT("avatar.presets")), 11, "Bold", Dim(), false)];
		Mid->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Pr];
	}

	// sağ: kimlik + sekmeler
	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.name")), TextEdit(&EditPilot.Name, false, FText::GetEmpty(), 260.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 10, 0, 10)[Tabs({ Tx(TEXT("avatar.tab_body")), Tx(TEXT("avatar.tab_face")), Tx(TEXT("avatar.tab_hair")), Tx(TEXT("avatar.tab_outfit")), Tx(TEXT("avatar.tab_acc")) },
		{ ESmvIcon::Pilot, ESmvIcon::User, ESmvIcon::Star, ESmvIcon::Shield, ESmvIcon::Gear }, &CrewTab)];
	switch (CrewTab)
	{
	case 0:
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Body, TEXT("body"), { TEXT("m"), TEXT("f") }, 2)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Build, TEXT("build"), { TEXT("slim"), TEXT("avg"), TEXT("broad") }, 3)];
		{
			TSharedRef<SHorizontalBox> Sk = SNew(SHorizontalBox);
			for (int32 k = 0; k < 8; ++k)
			{
				const FString V = FString::Printf(TEXT("s%d"), k + 1);
				Sk->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SSmvButton).Kind(ESmvBtn::Ghost).MinHeight(36.f).Selected_Lambda([Self, V]() { return Self->EditPilot.Avatar.Skin == V; })
					.OnClick([Self, V]() { Self->EditPilot.Avatar.Skin = V; Self->Refresh(); })
					.Extra()[SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(SmvHex(SmvSkins()[k]))]]
				];
			}
			Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.skin")), Sk)];
		}
		break;
	case 1:
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Face, TEXT("face"), { TEXT("oval"), TEXT("round"), TEXT("square"), TEXT("long"), TEXT("heart") }, 5)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Eyes, TEXT("eyes"), { TEXT("almond"), TEXT("round"), TEXT("narrow"), TEXT("wide") }, 4)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.eye_color")), ColorPick(&A.EyeColor, Re))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Brows, TEXT("brows"), { TEXT("straight"), TEXT("arched"), TEXT("angled"), TEXT("soft") }, 4)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.BrowW, TEXT("brow_w"), { TEXT("thin"), TEXT("medium"), TEXT("thick") }, 3)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Nose, TEXT("nose"), { TEXT("small"), TEXT("straight"), TEXT("wide"), TEXT("pointed") }, 4)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Mouth, TEXT("mouth"), { TEXT("neutral"), TEXT("smile"), TEXT("grin"), TEXT("serious") }, 4)];
		if (A.Body != TEXT("f")) Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Beard, TEXT("beard"), { TEXT("none"), TEXT("stubble"), TEXT("mustache"), TEXT("goatee"), TEXT("full") }, 3)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.freckles")), Toggle(&A.bFreckles, Re))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.mole")), Toggle(&A.bMole, Re))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Glasses, TEXT("glasses"), { TEXT("none"), TEXT("aviator"), TEXT("sun") }, 3)];
		break;
	case 2:
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Hair, TEXT("hair"), { TEXT("short"), TEXT("buzz"), TEXT("spiky"), TEXT("sidepart"), TEXT("curly"), TEXT("mohawk"), TEXT("bald"), TEXT("long"), TEXT("ponytail"), TEXT("bun"), TEXT("bob") }, 4)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.hair_color")), ColorPick(&A.HairColor, Re))];
		if (A.Headgear == TEXT("helmet")) Ed->AddSlot().AutoHeight().Padding(0, 3)[SmvUi::Text(Tx(TEXT("avatar.visor_open")), 10, "Regular", Mute())];
		break;
	case 3:
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Outfit, TEXT("outfit"), { TEXT("suit"), TEXT("jacket"), TEXT("casual") }, 3)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.suit")), ColorPick(&A.Suit, Re))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.patch")), Cycle(&A.Patch, { TEXT("none"), TEXT("star"), TEXT("wing"), TEXT("bolt"), TEXT("chevron"), TEXT("target"), TEXT("shield"), TEXT("cross") },
			{ Tx(TEXT("avatar.patch_none")), Tx(TEXT("avatar.patch_star")), Tx(TEXT("avatar.patch_wing")), Tx(TEXT("avatar.patch_bolt")), Tx(TEXT("avatar.patch_chevron")), Tx(TEXT("avatar.patch_target")), Tx(TEXT("avatar.patch_shield")), Tx(TEXT("avatar.patch_cross")) }, Re, 240.f))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.patch_color")), ColorPick(&A.PatchColor, Re))];
		break;
	default:
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Headgear, TEXT("headgear"), { TEXT("helmet"), TEXT("none"), TEXT("cap"), TEXT("headset") }, 4)];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.gear_color")), ColorPick(&A.Helmet, Re))];
		if (A.Headgear == TEXT("helmet") || A.Headgear.IsEmpty())
		{
			Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.visor")), ColorPick(&A.Visor, Re))];
			Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.mirror")), Toggle(&A.bMirror, Re))];
			Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.visor_open")), Toggle(&A.bVisorOpen, Re))];
		}
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.accessory")), Cycle(&A.Accessory, { TEXT("none"), TEXT("mic"), TEXT("crest"), TEXT("antenna"), TEXT("stripe") },
			{ Tx(TEXT("avatar.acc_none")), Tx(TEXT("avatar.acc_mic")), Tx(TEXT("avatar.acc_crest")), Tx(TEXT("avatar.acc_antenna")), Tx(TEXT("avatar.acc_stripe")) }, Re, 240.f))];
		break;
	}
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
			Self->Confirm(FSmvI18n::T(TEXT("dlg.delete_title")), FSmvI18n::Format(TEXT("dlg.delete_body"), { { TEXT("n"), Self->EditPilot.Name } }), FSmvI18n::T(TEXT("dlg.yes_delete")), true, [Self]()
			{
				Self->Data()->DeletePilot(Self->EditPilot.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); if (bOk && Self->Data()->Pilots().Num()) Self->EditPilot = Self->Data()->Pilots()[0]; Self->Refresh(); }));
			});
		})]
	];
	return Page(Tx(TEXT("menu.crew")), Tx(TEXT("menu.crew_sub")), ESmvIcon::Pilot, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[SNew(SBox).WidthOverride(280.f)[List]]
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
			Self->Confirm(FSmvI18n::T(TEXT("dlg.delete_title")), FSmvI18n::Format(TEXT("dlg.delete_body"), { { TEXT("n"), Self->EditTrack.Name } }), FSmvI18n::T(TEXT("dlg.yes_delete")), true, [Self]()
			{
				Self->Data()->DeleteTrack(Self->EditTrack.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); if (bOk && Self->Data()->Tracks().Num()) Self->EditTrack = Self->Data()->Tracks()[0]; Self->Refresh(); }));
			});
		})]
	];
	TSharedRef<SVerticalBox> Mid = SNew(SVerticalBox);
	// uydu haritası: konum seç / halka düzenleyici + yer araması
	Mid->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Chips(&MapMode, { TEXT("pick"), TEXT("rings"), TEXT("3d") }, { Tx(TEXT("map.mode_pick")), Tx(TEXT("map.mode_rings")), Tx(TEXT("map.mode_3d")) },
			{ ESmvIcon::Map, ESmvIcon::Flag, ESmvIcon::Globe }, [Self]() { Self->WorldPrevHash = 0; Self->Refresh(); })]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0, 6, 0)[TextEdit(&MapQuery, false, Tx(TEXT("map.search_ph")), 160.f)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(11).Icon(ESmvIcon::Globe).Text(Tx(TEXT("map.search")))
			.OnClick([Self]()
			{
				TWeakPtr<SSmvShell> W = StaticCastSharedRef<SSmvShell>(Self->AsShared());
				SSmvTileMap::Search(Self->MapQuery, [W](bool bOk, double La, double Lo, const FString& Name)
				{
					TSharedPtr<SSmvShell> S = W.Pin();
					if (!S.IsValid()) return;
					if (!bOk) { S->Toast(FSmvI18n::T(TEXT("map.not_found")).ToString(), 4); return; }
					S->EditTrack.Lat = La; S->EditTrack.Lon = Lo;
					if (S->TrackMap.IsValid()) S->TrackMap->CenterOn(La, Lo, 15);
					S->Toast(Name.Left(90), 6);
					S->Refresh();
				});
			})]
	];
	if (MapMode == TEXT("3d") && Scene() && FSmvUiPrefs::Get().bPreview3d && Scene()->GetPreviewTarget())
	{
		PreviewBrush.SetResourceObject(Scene()->GetPreviewTarget());
		PreviewBrush.ImageSize = FVector2D(960, 600);
		bPreviewVisible = true;
		Mid->AddSlot().AutoHeight()
		[
			SNew(SBox).WidthOverride(520.f).HeightOverride(325.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SBorder).BorderImage(Round(14.f)).BorderBackgroundColor(Hex(0x0b1222, 0.9f))]
				+ SOverlay::Slot()[SNew(SImage).Image(&PreviewBrush)]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(10.f)[SmvUi::Text(Tx(TEXT("map.live_hint")), 10, "Regular", Dim(), false)]
			]
		];
	}
	else
	Mid->AddSlot().AutoHeight()
	[
		SAssignNew(TrackMap, SSmvTileMap).Size(FVector2D(520.f, 340.f)).Lat(&EditTrack.Lat).Lon(&EditTrack.Lon).Gates(&EditTrack.Gates).RadiusM(&EditTrack.RadiusM)
		.RingMode_Lambda([Self]() { return Self->MapMode == TEXT("rings"); })
	];
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

namespace
{
	// görev geometri yardımcıları (web missions/geo.js)
	FVector2D MLocal(const FVector2D& C, const FVector2D& P) { const double Ml = 111320.0, Mo = 111320.0 * FMath::Cos(FMath::DegreesToRadians(C.X)); return FVector2D((P.Y - C.Y) * Mo, (P.X - C.X) * Ml); }
	FVector2D MGeo(const FVector2D& C, const FVector2D& L) { const double Ml = 111320.0, Mo = 111320.0 * FMath::Cos(FMath::DegreesToRadians(C.X)); return FVector2D(C.X + L.Y / Ml, C.Y + L.X / Mo); }

	TArray<FVector2D> MSweep(const FVector2D& C, const TArray<FVector2D>& Poly, double Spacing, double AngleDeg)
	{
		TArray<FVector2D> Out;
		if (Poly.Num() < 3) return Out;
		const double A = FMath::DegreesToRadians(AngleDeg), Ca = FMath::Cos(A), Sa = FMath::Sin(A);
		auto Rot = [Ca, Sa](const FVector2D& P) { return FVector2D(P.X * Ca + P.Y * Sa, -P.X * Sa + P.Y * Ca); };
		auto Unrot = [Ca, Sa](const FVector2D& P) { return FVector2D(P.X * Ca - P.Y * Sa, P.X * Sa + P.Y * Ca); };
		TArray<FVector2D> R;
		for (const FVector2D& P : Poly) R.Add(Rot(MLocal(C, P)));
		double MinY = 1e18, MaxY = -1e18;
		for (const FVector2D& P : R) { MinY = FMath::Min(MinY, P.Y); MaxY = FMath::Max(MaxY, P.Y); }
		bool bFlip = false;
		for (double Y = MinY + Spacing / 2; Y < MaxY; Y += Spacing)
		{
			TArray<double> Xs;
			for (int32 k = 0; k < R.Num(); ++k)
			{
				const FVector2D& P = R[k]; const FVector2D& Q = R[(k + 1) % R.Num()];
				if ((P.Y <= Y && Q.Y > Y) || (Q.Y <= Y && P.Y > Y)) Xs.Add(P.X + (Y - P.Y) * (Q.X - P.X) / (Q.Y - P.Y));
			}
			Xs.Sort();
			for (int32 k = 0; k + 1 < Xs.Num(); k += 2)
			{
				FVector2D S0(Xs[k], Y), S1(Xs[k + 1], Y);
				if (bFlip) Swap(S0, S1);
				Out.Add(MGeo(C, Unrot(S0))); Out.Add(MGeo(C, Unrot(S1)));
			}
			bFlip = !bFlip;
			if (Out.Num() > 480) break;
		}
		return Out;
	}

	TArray<FVector2D> MCorridor(const FVector2D& C, const TArray<FVector2D>& Line, double Width)
	{
		TArray<FVector2D> L, Lf, Rt, Out;
		if (Line.Num() < 2) return Out;
		for (const FVector2D& P : Line) L.Add(MLocal(C, P));
		for (int32 k = 0; k < L.Num(); ++k)
		{
			const FVector2D Q = L[FMath::Min(k + 1, L.Num() - 1)], O = L[FMath::Max(k - 1, 0)];
			FVector2D D = Q - O; const double Len = FMath::Max(D.Size(), 1e-6); const FVector2D N(-D.Y / Len, D.X / Len);
			Lf.Add(L[k] + N * Width * 0.5); Rt.Add(L[k] - N * Width * 0.5);
		}
		for (const FVector2D& P : Lf) Out.Add(MGeo(C, P));
		for (int32 k = Rt.Num() - 1; k >= 0; --k) Out.Add(MGeo(C, Rt[k]));
		return Out;
	}

	TSharedPtr<FJsonObject> MRoute(const FVector2D& Home, const TArray<FVector2D>& Pts, double Alt, const TArray<double>* AltPer = nullptr)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		int32 Seq = 0;
		auto Add = [&Arr, &Seq](int32 Cmd, double La, double Lo, double A)
		{
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetNumberField(TEXT("seq"), Seq++); P->SetNumberField(TEXT("cmd"), Cmd);
			P->SetNumberField(TEXT("lat"), FMath::RoundToDouble(La * 1e7) / 1e7); P->SetNumberField(TEXT("lon"), FMath::RoundToDouble(Lo * 1e7) / 1e7);
			P->SetNumberField(TEXT("alt_rel"), A); P->SetNumberField(TEXT("frame"), 3);
			Arr.Add(MakeShared<FJsonValueObject>(P));
		};
		Add(22, Home.X, Home.Y, Alt);
		for (int32 k = 0; k < Pts.Num(); ++k) Add(16, Pts[k].X, Pts[k].Y, AltPer && AltPer->IsValidIndex(k) ? (*AltPer)[k] : Alt);
		Add(21, Home.X, Home.Y, 0);
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetArrayField(TEXT("points"), Arr);
		return R;
	}
}

void SSmvShell::MPlanLoad()
{
	FMPlan P;
	P.LoadedFor = EditMission.Id + TEXT("|") + EditMission.Name;
	P.Kind = EditMission.Kind;
	P.Seed = FMath::RandRange(1, 2147483646);
	if (const TSharedPtr<FJsonObject> O = SmvJson::Parse(EditMission.PlanJson))
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(TEXT("shape"), A) && A)
			for (const TSharedPtr<FJsonValue>& V : *A) if (const TSharedPtr<FJsonObject> Q = V->AsObject()) P.Shape.Add(FVector2D(SmvJson::Num(Q, TEXT("lat")), SmvJson::Num(Q, TEXT("lon"))));
		if (O->TryGetArrayField(TEXT("order"), A) && A) for (const TSharedPtr<FJsonValue>& V : *A) P.Order.Add((int32)V->AsNumber());
		if (const TSharedPtr<FJsonObject> Pr = SmvJson::Obj(O, TEXT("params")))
		{
			P.Alt = SmvJson::Num(Pr, TEXT("alt"), 50); P.Width = SmvJson::Num(Pr, TEXT("width"), 40); P.Spacing = SmvJson::Num(Pr, TEXT("spacing"), 20);
			P.Angle = SmvJson::Num(Pr, TEXT("angle"), 0); P.Laps = (int32)SmvJson::Num(Pr, TEXT("laps"), 1);
		}
		if (const TSharedPtr<FJsonObject> D = SmvJson::Obj(O, TEXT("defend")))
		{
			P.Waves = (int32)SmvJson::Num(D, TEXT("waves"), 5); P.Interceptors = (int32)SmvJson::Num(D, TEXT("interceptors"), 10);
			P.Difficulty = SmvJson::Str(D, TEXT("difficulty"), TEXT("normal")); P.RadiusM = SmvJson::Num(D, TEXT("radius_m"), 30);
			P.PrepS = SmvJson::Num(D, TEXT("prep_s"), 45); P.Seed = (int32)SmvJson::Num(D, TEXT("seed"), P.Seed);
		}
	}
	MPlan = P;
	MZoom = 1.0; MPan = FVector2D::ZeroVector;
}

FString SSmvShell::MPlanSave() const
{
	const FMPlan& P = MPlan;
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("kind"), P.Kind);
	if (P.Kind == TEXT("defend"))
	{
		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetNumberField(TEXT("waves"), P.Waves); D->SetNumberField(TEXT("interceptors"), P.Interceptors); D->SetStringField(TEXT("difficulty"), P.Difficulty);
		D->SetNumberField(TEXT("radius_m"), P.RadiusM); D->SetNumberField(TEXT("prep_s"), P.PrepS); D->SetNumberField(TEXT("seed"), P.Seed);
		O->SetObjectField(TEXT("defend"), D);
		return SmvJson::ToString(O);
	}
	const FSmvTrack* Tr = nullptr;
	if (Data()) for (const FSmvTrack& T : Data()->Tracks()) if (T.Id == EditMission.TrackId) Tr = &T;
	const FVector2D Home = Tr ? FVector2D(Tr->Lat, Tr->Lon) : (P.Shape.Num() ? P.Shape[0] : FVector2D::ZeroVector);
	TSharedPtr<FJsonObject> Route;
	if (P.Kind == TEXT("corridor") && P.Shape.Num() >= 2) Route = MRoute(Home, P.Shape, P.Alt);
	else if (P.Kind == TEXT("free") && P.Shape.Num()) Route = MRoute(Home, P.Shape, P.Alt);
	else if ((P.Kind == TEXT("spray") || P.Kind == TEXT("survey")) && P.Shape.Num() > 2)
	{
		const TArray<FVector2D> L = MSweep(Home, P.Shape, FMath::Max(2.0, P.Spacing), P.Angle);
		if (L.Num()) Route = MRoute(Home, L, P.Kind == TEXT("spray") ? FMath::Min(P.Alt, 15.0) : P.Alt);
	}
	else if (P.Kind == TEXT("fpv_flags") && P.Shape.Num()) Route = MRoute(Home, P.Shape, P.Alt);
	else if (P.Kind == TEXT("fpv_gates") && Tr && Tr->Gates.Num())
	{
		TArray<FVector2D> Pts; TArray<double> Alts;
		TArray<int32> Ord = P.Order;
		if (Ord.Num() == 0) for (const FSmvGate& G : Tr->Gates) Ord.Add(G.I);
		for (int32 l = 0; l < FMath::Max(1, P.Laps); ++l)
			for (int32 I : Ord) for (const FSmvGate& G : Tr->Gates) if (G.I == I) { Pts.Add(FVector2D(G.Lat, G.Lon)); Alts.Add(G.AglM); }
		if (Pts.Num()) Route = MRoute(Home, Pts, P.Alt, &Alts);
	}
	if (Route.IsValid()) O->SetObjectField(TEXT("route"), Route);
	TArray<TSharedPtr<FJsonValue>> Sh;
	if (P.Kind != TEXT("fpv_gates"))
		for (const FVector2D& Q : P.Shape)
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetNumberField(TEXT("lat"), Q.X); J->SetNumberField(TEXT("lon"), Q.Y);
			Sh.Add(MakeShared<FJsonValueObject>(J));
		}
	O->SetArrayField(TEXT("shape"), Sh);
	TSharedRef<FJsonObject> Pr = MakeShared<FJsonObject>();
	Pr->SetNumberField(TEXT("alt"), P.Alt); Pr->SetNumberField(TEXT("width"), P.Width); Pr->SetNumberField(TEXT("spacing"), P.Spacing);
	Pr->SetNumberField(TEXT("angle"), P.Angle); Pr->SetNumberField(TEXT("laps"), P.Laps);
	O->SetObjectField(TEXT("params"), Pr);
	TArray<TSharedPtr<FJsonValue>> Od;
	for (int32 I : P.Order) Od.Add(MakeShared<FJsonValueNumber>(I));
	O->SetArrayField(TEXT("order"), Od);
	O->SetNumberField(TEXT("laps"), P.Laps);
	return SmvJson::ToString(O);
}

TSharedRef<SWidget> SSmvShell::MissionMap(float Wd, float Ht)
{
	SSmvShell* Self = this;
	TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(Wd, Ht));
	auto Track = [Self]() -> const FSmvTrack*
	{
		if (Self->Data()) for (const FSmvTrack& T : Self->Data()->Tracks()) if (T.Id == Self->EditMission.TrackId) return &T;
		return nullptr;
	};
	// ölçek: piksel / metre (pist yarıçapı ekrana sığar), kaydırma ve yakınlaştırma
	auto Scale = [Self, Track, Wd, Ht]() { const FSmvTrack* T = Track(); const double R = T ? FMath::Max(T->RadiusM, 200.0) : 1000.0; return FMath::Min(Wd, Ht) * 0.45 / R * Self->MZoom; };
	auto ToScr = [Self, Track, Scale, Wd, Ht](const FVector2D& LatLon) { const FSmvTrack* T = Track(); const FVector2D Ctr = T ? FVector2D(T->Lat, T->Lon) : LatLon; const FVector2D L = MLocal(Ctr, LatLon); const double K = Scale(); return FVector2D(Wd * 0.5 + (L.X + Self->MPan.X) * K, Ht * 0.5 - (L.Y + Self->MPan.Y) * K); };
	auto FromScr = [Self, Track, Scale, Wd, Ht](const FVector2D& S) { const FSmvTrack* T = Track(); const FVector2D Ctr = T ? FVector2D(T->Lat, T->Lon) : FVector2D::ZeroVector; const double K = Scale(); return MGeo(Ctr, FVector2D((S.X - Wd * 0.5) / K - Self->MPan.X, -(S.Y - Ht * 0.5) / K - Self->MPan.Y)); };
	C->Painter = [Self, Track, ToScr, Scale](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		DrBox(Out, L, G, FVector2D::ZeroVector, Sz, Hex(0x0b1424, 0.96f), Round(12.f));
		Out.PushClip(FSlateClippingZone(G));
		const FSmvTrack* T = Track();
		const double K = Scale();
		// ızgara (100 m)
		const FVector2D O = T ? ToScr(FVector2D(T->Lat, T->Lon)) : Sz * 0.5;
		const double Step = 100.0 * K;
		if (Step > 6.0)
		{
			for (double X = FMath::Fmod(O.X, Step); X < Sz.X; X += Step) DrSeg(Out, L + 1, G, FVector2D(X, 0), FVector2D(X, Sz.Y), FLinearColor(1, 1, 1, 0.05f), 1.f);
			for (double Y = FMath::Fmod(O.Y, Step); Y < Sz.Y; Y += Step) DrSeg(Out, L + 1, G, FVector2D(0, Y), FVector2D(Sz.X, Y), FLinearColor(1, 1, 1, 0.05f), 1.f);
		}
		if (T)
		{
			DrArc(Out, L + 1, G, O, (float)(T->RadiusM * K), 0.f, 360.f, Warn().CopyWithNewOpacity(0.5f), 1.5f);
			DrBox(Out, L + 3, G, O - FVector2D(6, 6), FVector2D(12, 12), Accent3(), Circle());
			DrText(Out, L + 3, G, TEXT("H"), O, Font(8, "Bold"), Hex(0x0b1424), 0);
			for (const FSmvGate& Gt : T->Gates)
			{
				const FVector2D S = ToScr(FVector2D(Gt.Lat, Gt.Lon));
				const int32 Oi = Self->MPlan.Order.IndexOfByKey(Gt.I);
				const bool bOn = Self->MPlan.Kind == TEXT("fpv_gates") && (Self->MPlan.Order.Num() == 0 || Oi >= 0);
				DrArc(Out, L + 3, G, S, 10.f, 0.f, 360.f, bOn ? Accent() : FLinearColor(0.58f, 0.64f, 0.72f, 0.5f), 3.f);
				DrText(Out, L + 4, G, Self->MPlan.Kind == TEXT("fpv_gates") && Self->MPlan.Order.Num() ? (Oi >= 0 ? FString::FromInt(Oi + 1) : FString(TEXT("·"))) : FString::FromInt(Gt.I + 1), S, Font(9, "Bold"), Fg(), 0);
			}
		}
		const FMPlan& P = Self->MPlan;
		// poligon / koridor
		TArray<FVector2D> Poly;
		const FVector2D Home = T ? FVector2D(T->Lat, T->Lon) : FVector2D::ZeroVector;
		if (P.Kind == TEXT("corridor")) Poly = MCorridor(Home, P.Shape, P.Width);
		else if ((P.Kind == TEXT("spray") || P.Kind == TEXT("survey")) && P.Shape.Num() > 2) Poly = P.Shape;
		if (Poly.Num() > 2)
		{
			TArray<FVector2D> S; for (const FVector2D& Q : Poly) S.Add(ToScr(Q)); S.Add(S[0]);
			DrLine(Out, L + 2, G, S, Accent2(), 2.f);
		}
		// rota önizlemesi
		TArray<FVector2D> Rt;
		if (P.Kind == TEXT("spray") || P.Kind == TEXT("survey")) Rt = MSweep(Home, P.Shape, FMath::Max(2.0, P.Spacing), P.Angle);
		else if (P.Kind != TEXT("defend") && P.Kind != TEXT("fpv_gates")) Rt = P.Shape;
		if (Rt.Num())
		{
			TArray<FVector2D> S; S.Add(ToScr(Home)); for (const FVector2D& Q : Rt) S.Add(ToScr(Q)); S.Add(ToScr(Home));
			DrLine(Out, L + 3, G, S, Accent().CopyWithNewOpacity(0.85f), 2.f);
		}
		for (int32 k = 0; k < P.Shape.Num(); ++k)
		{
			const FVector2D S = ToScr(P.Shape[k]);
			const float R = k == Self->MHover ? 8.f : 6.f;
			DrBox(Out, L + 4, G, S - FVector2D(R, R), FVector2D(2 * R, 2 * R), P.Kind == TEXT("fpv_flags") ? Warn() : FLinearColor::White, Circle());
			if (P.Kind == TEXT("fpv_flags") || P.Kind == TEXT("free")) DrText(Out, L + 5, G, FString::FromInt(k + 1), S - FVector2D(0, 14), Font(9, "Bold"), Fg(), 0);
		}
		if (P.Kind == TEXT("defend"))
		{
			for (int32 k = 1; k <= 3; ++k) DrArc(Out, L + 3, G, O, (float)(k * 1000.0 / 3.0 * K), 0.f, 360.f, Hex(0x3dff8a, 0.5f), 1.f);
			uint32 A = (uint32)FMath::Abs(P.Seed) % 360;
			for (int32 k = 0; k < FMath::Min(8, 2 + P.Waves); ++k)
			{
				A = (uint32)FMath::Fmod(A * 137.508 + 61.0, 360.0);
				const double Ra = FMath::DegreesToRadians((double)A);
				const FVector2D D(FMath::Sin(Ra), -FMath::Cos(Ra));
				DrSeg(Out, L + 3, G, O + D * Sz.Y * 0.42, O + D * Sz.Y * 0.1, Hex(0xff5a46), 2.f);
			}
		}
		DrText(Out, L + 5, G, FSmvI18n::T(FString(TEXT("missions.hint_")) + P.Kind).ToString(), FVector2D(12.f, Sz.Y - 14.f), Font(10), Dim(), -1);
		Out.PopClip();
	};
	C->OnHover = [Self, ToScr](const FVector2D& P)
	{
		int32 Best = -1; double Bd = 14.0;
		for (int32 k = 0; k < Self->MPlan.Shape.Num(); ++k) { const double D = (ToScr(Self->MPlan.Shape[k]) - P).Size(); if (D < Bd) { Bd = D; Best = k; } }
		Self->MHover = Best;
	};
	C->OnDrag = [Self, ToScr, FromScr, Track](const FGeometry& G, const FVector2D& P, int32 Phase)
	{
		FMPlan& M = Self->MPlan;
		const bool bRight = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::RightMouseButton);
		const bool bShift = FSlateApplication::Get().GetModifierKeys().IsShiftDown();
		static FVector2D Last;
		if (Phase == 0)
		{
			Last = P;
			if (bShift) { Self->MDrag = -2; return; }   // kaydır
			int32 Hit = -1; double Bd = 14.0;
			for (int32 k = 0; k < M.Shape.Num(); ++k) { const double D = (ToScr(M.Shape[k]) - P).Size(); if (D < Bd) { Bd = D; Hit = k; } }
			if (bRight) { if (Hit >= 0) M.Shape.RemoveAt(Hit); Self->MDrag = -1; return; }
			if (M.Kind == TEXT("fpv_gates"))
			{
				if (const FSmvTrack* T = Track())
					for (const FSmvGate& Gt : T->Gates)
						if ((ToScr(FVector2D(Gt.Lat, Gt.Lon)) - P).Size() < 16.0) { const int32 Oi = M.Order.IndexOfByKey(Gt.I); if (Oi >= 0) M.Order.RemoveAt(Oi); else M.Order.Add(Gt.I); }
				Self->MDrag = -1;
				return;
			}
			if (M.Kind == TEXT("defend")) return;
			if (Hit >= 0) { Self->MDrag = Hit; return; }
			if (M.Shape.Num() < 200) { M.Shape.Add(FromScr(P)); FSmvUiSound::Play(ESmvSnd::Toggle); }
			Self->MDrag = -1;
			return;
		}
		if (Phase == 1)
		{
			if (Self->MDrag == -2) { const FVector2D D = P - Last; Last = P;
				const FSmvTrack* T = Track(); const double R = T ? FMath::Max(T->RadiusM, 200.0) : 1000.0; const double Kp = FMath::Min(G.GetLocalSize().X, G.GetLocalSize().Y) * 0.45 / R * Self->MZoom;
				Self->MPan += FVector2D(D.X / Kp, -D.Y / Kp); return; }
			if (M.Shape.IsValidIndex(Self->MDrag)) M.Shape[Self->MDrag] = FromScr(P);
			return;
		}
		Self->MDrag = -1;
	};
	return SNew(SBox).WidthOverride(Wd).HeightOverride(Ht)[C];
}

TSharedRef<SWidget> SSmvShell::Missions()
{
	USmvDataService* D = Data();
	if (!D) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	const TArray<FString> Kinds = { TEXT("corridor"), TEXT("free"), TEXT("spray"), TEXT("fpv_flags"), TEXT("survey"), TEXT("fpv_gates"), TEXT("defend") };
	const TArray<ESmvIcon> KindI = { ESmvIcon::Map, ESmvIcon::Route, ESmvIcon::Rain, ESmvIcon::Flag, ESmvIcon::Camera, ESmvIcon::Fpv, ESmvIcon::Shield };
	if (MPlan.LoadedFor != EditMission.Id + TEXT("|") + EditMission.Name) MPlanLoad();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvMission& X : D->Missions())
	{
		const FSmvMission Copy = X;
		const int32 Ki = Kinds.IndexOfByKey(X.Kind);
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ListItem(FText::FromString(X.Name), FSmvI18n::T(FString(TEXT("mission.")) + X.Kind),
			KindI.IsValidIndex(Ki) ? KindI[Ki] : ESmvIcon::Mission, X.Id == EditMission.Id, false, [Self, Copy]() { Self->EditMission = Copy; Self->MPlanLoad(); Self->Refresh(); }, Accent2())];
	}
	List->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Btn(Tx(TEXT("missions.new")), [Self]()
	{
		FSmvMission M; M.Name = FSmvI18n::T(TEXT("missions.new_name")).ToString();
		if (const FSmvTrack* T = Self->Data()->DefaultTrack()) M.TrackId = T->Id;
		Self->EditMission = M; Self->MPlanLoad(); Self->Refresh();
	}, false, true, ESmvIcon::Plus)];
	TArray<FString> TrackIds; TArray<FText> TrackL;
	for (const FSmvTrack& T : D->Tracks()) { TrackIds.Add(T.Id); TrackL.Add(FText::FromString(T.Name)); }
	TArray<FText> KindL; for (const FString& K : Kinds) KindL.Add(FSmvI18n::T(FString(TEXT("mission.")) + K));
	auto Mf = [](double V) { return FString::Printf(TEXT("%.0f m"), V); };
	auto Df = [](double V) { return FString::Printf(TEXT("%.0f°"), V); };
	TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.name")), TextEdit(&EditMission.Name, false, FText::GetEmpty(), 240.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.track")), Cycle(&EditMission.TrackId, TrackIds, TrackL, [Self]() { Self->Refresh(); }, 260.f))];
	Ed->AddSlot().AutoHeight().Padding(0, 3)[Chips(&MPlan.Kind, Kinds, KindL, KindI, [Self]()
	{
		Self->EditMission.Kind = Self->MPlan.Kind;
		Self->Refresh();
	}, 4)];
	FMPlan& P = MPlan;
	EditMission.Kind = P.Kind;
	if (P.Kind != TEXT("spray") && P.Kind != TEXT("defend")) Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.alt")), Slider(&P.Alt, 5, 300, 5, Mf))];
	if (P.Kind == TEXT("corridor")) Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.width")), Slider(&P.Width, 5, 300, 5, Mf))];
	if (P.Kind == TEXT("spray"))
	{
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.spacing")), Slider(&P.Spacing, 2, 50, 1, Mf))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.angle")), Slider(&P.Angle, 0, 179, 1, Df))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.alt")), Slider(&P.Alt, 2, 15, 1, Mf))];
	}
	if (P.Kind == TEXT("survey"))
	{
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.spacing")), Slider(&P.Spacing, 10, 200, 5, Mf))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.angle")), Slider(&P.Angle, 0, 179, 1, Df))];
	}
	if (P.Kind == TEXT("fpv_gates")) Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.laps")), SliderI(&P.Laps, 1, 10))];
	if (P.Kind == TEXT("defend"))
	{
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("race.level")), Chips(&P.Difficulty, { TEXT("easy"), TEXT("normal"), TEXT("hard") }, { Tx(TEXT("level.easy")), Tx(TEXT("level.normal")), Tx(TEXT("level.hard")) }, {}))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.waves")), SliderI(&P.Waves, 1, 10))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.interceptors")), SliderI(&P.Interceptors, 1, 20))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.def_radius")), Slider(&P.RadiusM, 10, 100, 5, Mf))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("missions.prep")), Slider(&P.PrepS, 15, 300, 5, [](double V) { return FString::Printf(TEXT("%.0f s"), V); }))];
		Ed->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("race.seed")), SliderI(&P.Seed, 1, 999999))];
	}
	Ed->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s: %d"), *Tx(TEXT("missions.points")).ToString(), P.Shape.Num())), 11, "Regular", Dim())];
	Ed->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(Tx(TEXT("missions.map_help")), 10, "Regular", Mute())];
	Ed->AddSlot().AutoHeight().Padding(0, 14, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("common.save")), [Self]()
		{
			Self->EditMission.Kind = Self->MPlan.Kind;
			Self->EditMission.PlanJson = Self->MPlanSave();
			Self->Data()->SaveMission(Self->EditMission, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.saved")).ToString() : M, bOk ? 6 : 3); Self->MPlan.LoadedFor.Empty(); Self->Refresh(); }));
		}, true, true, ESmvIcon::Save)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(Tx(TEXT("missions.clear")), [Self]() { Self->MPlan.Shape.Reset(); Self->MPlan.Order.Reset(); Self->Refresh(); }, false, true, ESmvIcon::Trash)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(FText::FromString(TEXT("+")), [Self]() { Self->MZoom = FMath::Min(Self->MZoom * 1.4, 20.0); }, false, true)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(FText::FromString(TEXT("−")), [Self]() { Self->MZoom = FMath::Max(Self->MZoom / 1.4, 0.2); }, false, true)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Trash).Text(Tx(TEXT("common.delete"))).IsEnabled(!EditMission.Id.IsEmpty()).OnClick([Self]()
		{
			Self->Confirm(FSmvI18n::T(TEXT("dlg.delete_title")), FSmvI18n::Format(TEXT("dlg.delete_body"), { { TEXT("n"), Self->EditMission.Name } }), FSmvI18n::T(TEXT("dlg.yes_delete")), true, [Self]()
			{
				Self->Data()->DeleteMission(Self->EditMission.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& M) { Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : M, bOk ? 6 : 3); Self->EditMission = FSmvMission(); Self->MPlanLoad(); Self->Refresh(); }));
			});
		})]
	];
	return Page(Tx(TEXT("menu.missions")), Tx(TEXT("menu.missions_sub")), ESmvIcon::Mission, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0)[SNew(SBox).WidthOverride(280.f)[List]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0)[MissionMap(760.f, 560.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Ed, FMargin(18.f))]);
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
	static const TCHAR* StepKeys[] = { TEXT("wiz.s_vehicle"), TEXT("wiz.s_map"), TEXT("wiz.s_env"), TEXT("wiz.s_control"), TEXT("wiz.s_rules"), TEXT("wiz.s_gcs"), TEXT("wiz.s_adv"), TEXT("wiz.s_go") };
	static const ESmvIcon StepIcons[] = { ESmvIcon::Drone, ESmvIcon::Map, ESmvIcon::Sun, ESmvIcon::Gamepad, ESmvIcon::Flag, ESmvIcon::Shield, ESmvIcon::Gear, ESmvIcon::Play };
	const int32 N = 8;
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
	case 5: Body = WizGcs(); break;
	case 6: Body = WizAdvanced(); break;
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
		// rüzgâra karşı kalkış önerisi (web takeoffAdvice): baş/yan/kuyruk bileşeni + "Rüzgâra karşı çevir" (pist yönü)
		const FSmvTrack& Tk = R.Track;
		if (R.Launch.Method != TEXT("hand") && Tk.WindMs > 0.3)
		{
			const double Hdg = Tk.World.RunwayHeading;
			double Rel = FMath::Fmod(Tk.WindDir - Hdg, 360.0); if (Rel > 180) Rel -= 360; if (Rel <= -180) Rel += 360;
			const double Head = Tk.WindMs * FMath::Cos(FMath::DegreesToRadians(Rel)), Cross = Tk.WindMs * FMath::Sin(FMath::DegreesToRadians(Rel));
			const int32 Sug = ((FMath::RoundToInt(Tk.WindDir / 5.0) * 5) % 360 + 360) % 360;
			const double Off = FMath::Fmod(FMath::Fmod(Sug - Hdg, 360.0) + 540.0, 360.0) - 180.0;
			const bool bOk = Head >= -1.0 && FMath::Abs(Cross) <= 6.0;
			const FText Comp = Head >= 0 ? FSmvI18n::Format(TEXT("wiz.wind_head"), { { TEXT("h"), FString::Printf(TEXT("%.1f"), Head) } })
				: FSmvI18n::Format(TEXT("wiz.wind_tail"), { { TEXT("h"), FString::Printf(TEXT("%.1f"), -Head) } });
			L->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Icon(ESmvIcon::Wind, 18.f, bOk ? Accent3() : Warn())]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FSmvI18n::Format(TEXT("wiz.wind_adv"), {
					{ TEXT("ms"), FString::Printf(TEXT("%.1f"), Tk.WindMs) }, { TEXT("dir"), FString::Printf(TEXT("%.0f"), Tk.WindDir) },
					{ TEXT("hdg"), FString::Printf(TEXT("%.0f"), Hdg) }, { TEXT("comp"), Comp.ToString() }, { TEXT("x"), FString::Printf(TEXT("%.1f"), FMath::Abs(Cross)) } }), 11, "Regular", bOk ? Dim() : Warn())]
			];
			if (FMath::Abs(Off) >= 15.0)
				L->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 6, 0, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(12).Icon(ESmvIcon::Wind)
					.Text(FSmvI18n::Format(TEXT("wiz.wind_turn"), { { TEXT("s"), FString::FromInt(Sug) } }))
					.OnClick([Self, F, Sug]() { F->Draft.Track.World.RunwayHeading = Sug; Self->Toast(FSmvI18n::T(TEXT("wfx.into_wind")).ToString(), 6); Self->Refresh(); })];
		}
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
	{
		double Kml = -1, Wt = -1;
		if (FSmvUiPrefs::GetMeasured(R.Drone.Id, Kml, Wt))
		{
			const FString Txt = (Kml > 0 ? FString::Printf(TEXT("%.1f km/L"), Kml) : FString()) + (Kml > 0 && Wt > 0 ? TEXT(" · ") : TEXT("")) + (Wt > 0 ? FString::Printf(TEXT("%.0f W"), Wt) : FString());
			L->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 8, 0, 0)
			[
				SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(12).Icon(ESmvIcon::Star).Accent(Hex(0xfbbf24))
				.Text(FText::FromString(FSmvI18n::T(TEXT("power.recommended")).ToString() + TEXT(": ") + Txt))
				.OnClick([Self, F, Kml, Wt]() { if (Kml > 0) F->Draft.Power.FuelKmPerL = FMath::RoundToDouble(Kml * 10.0) / 10.0; if (Wt > 0) F->Draft.Power.ElecW = FMath::RoundToDouble(Wt); Self->Toast(FSmvI18n::T(TEXT("power.recommended_applied")).ToString(), 6); Self->Refresh(); })
			];
		}
	}

	Rt->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(Tx(TEXT("wizard.sim")), Accent2())];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wizard.camera")), Chips(&R.Camera, { TEXT("day"), TEXT("thermal"), TEXT("night"), TEXT("none") },
		{ Tx(TEXT("cam.day")), Tx(TEXT("cam.thermal")), Tx(TEXT("cam.night")), Tx(TEXT("cam.none")) }, { ESmvIcon::Sun, ESmvIcon::Bolt, ESmvIcon::Moon, ESmvIcon::Camera }, [Self]() { Self->Refresh(); }, 2))];
	if (R.Camera == TEXT("thermal"))
		Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("camera.palette")), Chips(&FSmvUiPrefs::Get().ThermalPalette, { TEXT("white"), TEXT("black"), TEXT("iron") },
			{ Tx(TEXT("camera.pal_white")), Tx(TEXT("camera.pal_black")), Tx(TEXT("camera.pal_iron")) }, {}, []() { FSmvUiPrefs::Get().Save(); }, 3))];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wizard.speedup")), Chips(&WizSpeed, { TEXT("0.25"), TEXT("0.5"), TEXT("1.0"), TEXT("2.0"), TEXT("4.0"), TEXT("8.0") },
		{ FText::FromString(TEXT("0,25×")), FText::FromString(TEXT("0,5×")), FText::FromString(TEXT("1×")), FText::FromString(TEXT("2×")), FText::FromString(TEXT("4×")), FText::FromString(TEXT("8×")) }, {},
		[Self, F]() { F->Draft.Speedup = FCString::Atod(*Self->WizSpeed); }, 3))];
	const bool bIn = Client() && Client()->IsLoggedIn();
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wizard.backend")), bIn
		? Chips(&WizBackend, { TEXT("local"), TEXT("cloud") }, { Tx(TEXT("backend.local")), Tx(TEXT("backend.cloud")) }, { ESmvIcon::Monitor, ESmvIcon::Cloud },
			[Self, F]() { F->Draft.Backend = Self->WizBackend == TEXT("cloud") ? ESmvFlightBackend::Cloud : ESmvFlightBackend::LocalSitl; })
		: SmvUi::Text(Tx(TEXT("backend.local_guest")), 12, "Regular", Fg()))];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("wiz.record")), Toggle(&R.bRecordBin))];
	Rt->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("video.wizard")), Toggle(&FSmvUiPrefs::Get().bRecordVideo, []() { FSmvUiPrefs::Get().Save(); }), Tx(TEXT("video.hint")))];
	Rt->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SmvUi::Text(USmvLocalFlightLink::SitlAvailable() ? Tx(TEXT("wizard.sitl_note")) : Tx(TEXT("menu.sitl_missing")), 10, "Regular", Mute())];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 18, 0)[Glass(L, FMargin(20.f))]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Rt, FMargin(20.f))];
}

TSharedRef<SWidget> SSmvShell::WizGcs()
{
	SSmvShell* Self = this;
	FSmvSite& Si = Flow->Draft.Site;
	auto Km = [](double V) { return FString::Printf(TEXT("%.1f km"), V); };
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(FText::FromString(FSmvI18n::T(TEXT("site.title")).ToString() + TEXT("  ·  ") + FSmvI18n::T(TEXT("site.optional")).ToString()), Accent3())];
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[SmvUi::Text(Tx(TEXT("site.desc")), 11, "Regular", Dim())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("site.station")), Toggle(&Si.bStation, [Self]() { Self->Refresh(); }), Tx(TEXT("site.station_desc")))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("site.radar")), Toggle(&Si.bRadar, [Self]() { Self->Refresh(); }), Tx(TEXT("site.radar_desc")))];
	if (Si.bRadar) V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("site.coverage")), Slider(&Si.RadarKm, 1, 50, 1, Km))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("site.eo")), Toggle(&Si.bEo, [Self]() { Self->Refresh(); }), Tx(TEXT("site.eo_desc")))];
	if (Si.bEo) V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("site.zoom_range")), Slider(&Si.EoKm, 0.5, 15, 0.5, Km))];
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[SmvUi::Text(FText::FromString(FSmvI18n::T(TEXT("site.radar_title")).ToString() + TEXT(" · ") + FSmvI18n::T(TEXT("site.eo_title")).ToString()), 10, "Regular", Mute())];
	// şematik (kuzey yukarı): kalkış, istasyon, kule, kapsama
	TSharedRef<SSmvCanvas> Sch = SNew(SSmvCanvas).Size(FVector2D(360, 360));
	Sch->Painter = [Self](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		if (!Self->Flow.IsValid()) return;
		const FSmvSite& S = Self->Flow->Draft.Site;
		const double Hd = FMath::DegreesToRadians(Self->Flow->Draft.Track.World.RunwayHeading);
		const FVector2D Sz = FVector2D(G.GetLocalSize()), C = Sz * 0.5;
		DrBox(Out, L, G, FVector2D::ZeroVector, Sz, Hex(0x0b1222, 0.9f), Round(14.f));
		const float K = (float)Sz.X / 260.f;   // ~260 m alan
		auto P = [&](double F, double R) { const double E = F * FMath::Sin(Hd) + R * FMath::Cos(Hd), N = F * FMath::Cos(Hd) - R * FMath::Sin(Hd); return C + FVector2D(E, -N) * K; };
		DrSeg(Out, L + 1, G, P(-60, 0), P(110, 0), FLinearColor(1, 1, 1, 0.25f), 6.f);   // pist / kalkış ekseni
		DrBox(Out, L + 2, G, P(0, 0) - FVector2D(7, 7), FVector2D(14, 14), Accent(), Circle());
		DrText(Out, L + 3, G, FSmvI18n::T(TEXT("site.pad")).ToString(), P(0, 0) + FVector2D(0, 16), Font(9, "Bold"), Accent(), 0);
		if (S.bStation) { DrBox(Out, L + 2, G, P(-40, 55) - FVector2D(10, 5), FVector2D(20, 10), Accent3(), nullptr); DrText(Out, L + 3, G, FSmvI18n::T(TEXT("site.station_short")).ToString(), P(-40, 55) + FVector2D(0, 14), Font(9, "Bold"), Accent3(), 0); }
		if (S.bRadar || S.bEo)
		{
			DrBox(Out, L + 2, G, P(-40, 85) - FVector2D(6, 6), FVector2D(12, 12), Warn(), nullptr);
			DrText(Out, L + 3, G, FSmvI18n::T(TEXT("site.tower")).ToString(), P(-40, 85) + FVector2D(0, 14), Font(9, "Bold"), Warn(), 0);
			if (S.bRadar) DrArc(Out, L + 1, G, P(-40, 85), (float)C.X - 6.f, 0.f, 360.f, Hex(0x34d399, 0.5f), 1.5f);
		}
		DrText(Out, L + 3, G, FSmvI18n::T(TEXT("site.schema")).ToString(), FVector2D(10, 14), Font(9, "Bold"), Dim(), -1);
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 18, 0)[Glass(V, FMargin(20.f))]
		+ SHorizontalBox::Slot().AutoWidth()[Glass(Sch, FMargin(14.f))];
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
		.OnClick([this, F, G]()
		{
			Confirm(FSmvI18n::T(TEXT("dlg.end_title")), FSmvI18n::T(TEXT("dlg.end_body")), FSmvI18n::T(TEXT("dlg.yes_end")), true,
				[F, G]() { if (G.IsValid() && G->IsPausedMenu()) G->TogglePause(); if (F.IsValid()) F->EndFlight(TEXT("user")); });
		})];
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
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FSmvI18n::T(TEXT("badge.") + Bd).ToString() == TEXT("badge.") + Bd ? FText::FromString(Bd) : FSmvI18n::T(TEXT("badge.") + Bd), 12, "Bold", Fg(), false)]
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
	// podyum + oyun puanı (yarış / it dalaşı)
	if (Flow->LastScore >= 0)
	{
		const int32 Score = Flow->LastScore;
		const double At = SummaryAt;
		B->AddSlot().AutoHeight().Padding(6, 16, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)[Icon(ESmvIcon::Trophy, 34.f, Warn())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(Tx(TEXT("summary.score")), 11, "Bold", Dim(), false)]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(Font(34, "Black")).ColorAndOpacity(Warn())
					.Text_Lambda([Score, At]()
					{
						const float K = ReducedMotion() ? 1.f : FMath::Clamp((float)(FPlatformTime::Seconds() - At) / 1.6f, 0.f, 1.f);
						return FText::AsNumber(FMath::RoundToInt(Score * (1.f - FMath::Pow(1.f - K, 3.f))));
					})
				]
			]
		];
	}
	if (Flow->LastPodium.Num() > 1)
	{
		const TArray<FSmvPodiumRow>& P = Flow->LastPodium;
		TSharedRef<SHorizontalBox> Pod = SNew(SHorizontalBox);
		const int32 Order[3] = { 1, 0, 2 };   // 2. · 1. · 3.
		const float Heights[3] = { 90.f, 130.f, 64.f };
		const uint32 Cols[3] = { 0xcbd5e1, 0xfbbf24, 0xd97706 };
		for (int32 k = 0; k < 3; ++k)
		{
			const int32 Ix = Order[k];
			if (!P.IsValidIndex(Ix)) continue;
			const FSmvPodiumRow& R = P[Ix];
			Pod->AddSlot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 8, 0)
			[
				SNew(SBox).WidthOverride(170.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Icon(R.bMe ? ESmvIcon::Pilot : ESmvIcon::Drone, 30.f, R.bMe ? Accent() : Dim())]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4)[SmvUi::Text(FText::FromString(R.Name), 12, "Bold", R.bMe ? Accent() : Fg(), false)]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).HeightOverride(Heights[Ix])
						[
							SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Hex(Cols[Ix], 0.85f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text(FText::AsNumber(Ix + 1)).Font(Font(30, "Black")).ColorAndOpacity(Hex(0x111827))
							]
						]
					]
				]
			];
		}
		B->AddSlot().AutoHeight().Padding(6, 16, 0, 0)[Pod];
		for (int32 r = 0; r < P.Num(); ++r)
		{
			const FSmvPodiumRow& R = P[r];
			const FString T = R.FinishT >= 0 ? FString::Printf(TEXT("%d:%05.2f"), (int32)(R.FinishT / 60), FMath::Fmod(R.FinishT, 60.0)) : Tx(TEXT("summary.dnf")).ToString();
			B->AddSlot().AutoHeight().Padding(6, 3, 0, 0)
			[
				SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(R.bMe ? Accent().CopyWithNewOpacity(0.15f) : Card()).Padding(FMargin(14.f, 6.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)[SmvUi::Text(FText::AsNumber(r + 1), 13, "Bold", Warn(), false)]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SmvUi::Text(FText::FromString(R.Name), 13, R.bMe ? "Bold" : "Regular", Fg(), false)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(14, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%d / %d"), R.Passed, R.Total)), 12, "Regular", Dim(), false)]
					+ SHorizontalBox::Slot().AutoWidth()[SmvUi::Text(FText::FromString(T), 13, "Bold", Fg(), false)]
				]
			];
		}
	}
	// FPV zaman denemesi sonucu
	if (Flow->LastFpv.bValid)
	{
		const USmvFlow::FFpvResult& Fr = Flow->LastFpv;
		auto Tm = [](double V) { return V < 0 ? FString(TEXT("—")) : FString::Printf(TEXT("%d:%05.2f"), (int32)(V / 60.0), FMath::Fmod(V, 60.0)); };
		const FSmvFpvCourse* Co = SmvFpv::Find(Fr.CourseId);
		TSharedRef<SVerticalBox> Fv = SNew(SVerticalBox);
		Fv->AddSlot().AutoHeight()[Section(FText::FromString(FString::Printf(TEXT("%s · %s"), *Tx(TEXT("fpv.result_tt")).ToString(), Co ? *Co->Name().ToString() : *Fr.CourseId)), Hex(0xf472b6))];
		Fv->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 18, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(Tx(TEXT("fpv.total_time")), 11, "Bold", Dim(), false)]
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Fr.bFinished ? Tm(Fr.TotalS) : Tx(TEXT("fpv.dnf")).ToString())).Font(Font(30, "Black")).ColorAndOpacity(Fg())]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Fr.bNewPb
				? AsW<SBorder>(SNew(SBorder).BorderImage(Round(14.f)).BorderBackgroundColor(Hex(0xa855f7, 0.35f)).Padding(FMargin(14.f, 6.f))[SmvUi::Text(Tx(TEXT("fpv.new_pb")), 14, "Black", FLinearColor::White, false)])
				: SmvUi::Text(Fr.PrevPbS > 0 ? FSmvI18n::Format(TEXT("fpv.pb_keep"), { { TEXT("t"), Tm(Fr.PrevPbS) }, { TEXT("d"), FString::Printf(TEXT("%+.2f s"), Fr.TotalS - Fr.PrevPbS) } }) : Tx(TEXT("fpv.pb_first")), 12, "Regular", Dim(), false)
			]
		];
		Fv->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s %s · %s %d · %s %d · %s +%.1f s · %s %d"),
			*Tx(TEXT("fpv.best_lap")).ToString(), *Tm(Fr.BestLapS), *Tx(TEXT("fpv.misses")).ToString(), Fr.Misses, *Tx(TEXT("fpv.hits")).ToString(), Fr.Hits,
			*Tx(TEXT("fpv.penalty")).ToString(), Fr.PenS, *Tx(TEXT("summary.score")).ToString(), Fr.Score)), 12, "Regular", Fg())];
		for (int32 r = 0; r < Fr.LapTimes.Num(); ++r)
		{
			FString Secs;
			if (Fr.Sectors.IsValidIndex(r)) for (int32 k = 0; k < Fr.Sectors[r].Num(); ++k) Secs += FString::Printf(TEXT("   S%d %.2f"), k + 1, Fr.Sectors[r][k]);
			Fv->AddSlot().AutoHeight().Padding(0, 3, 0, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s %d   %s%s"), *Tx(TEXT("fpv.lap")).ToString(), r + 1, *Tm(Fr.LapTimes[r]), *Secs)), 12, "Mono", Fg(), false)];
		}
		B->AddSlot().AutoHeight().Padding(6, 14, 0, 0)[Glass(Fv, FMargin(18.f))];
	}
	if (S.Badges.Num()) B->AddSlot().AutoHeight().Padding(6, 12, 0, 0)[Badges];
	if (Flow->LastPath.Num() > 1)
		B->AddSlot().AutoHeight().Padding(6, 14, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[RouteMap(&Flow->LastPath, 420.f, 260.f)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(14, 0, 0, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[SNew(SSmvButton).Icon(ESmvIcon::Book).Text(Tx(TEXT("report.open")))
					.OnClick([this, F]() { if (F.IsValid()) OpenReport(F->LastRequest, F->LastSummary, F->LastPath); })]
				+ SVerticalBox::Slot().AutoHeight()[SNew(SSmvButton).Icon(ESmvIcon::Replay).Text(Tx(TEXT("replay.play")))
					.IsEnabled(!Flow->LastReplayFile.IsEmpty())
					.OnClick([F]() { if (F.IsValid() && !F->LastReplayFile.IsEmpty()) F->StartReplay(F->LastReplayFile, 1.f); })]
			]
		];
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
		// 3B avatar (menü sahnesinde baş-omuz anlık görüntüsü; yoksa çizilmiş portre)
		TSharedRef<SWidget> Avatar = SNullWidget::NullWidget;
		{
			UTextureRenderTarget2D* Rt = (R.bHasAvatar && Scene() && FSmvUiPrefs::Get().bPreview3d) ? Scene()->AvatarSnap(R.Avatar) : nullptr;
			if (Rt)
			{
				TSharedPtr<FSlateBrush>& Br = AvatarBrushes.FindOrAdd(Rt->GetName());
				if (!Br.IsValid()) { Br = MakeShared<FSlateBrush>(); Br->DrawAs = ESlateBrushDrawType::Image; Br->SetResourceObject(Rt); Br->ImageSize = FVector2D(56, 56); }
				Avatar = SNew(SBox).WidthOverride(56.f).HeightOverride(56.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(Medal.CopyWithNewOpacity(0.22f))]
						+ SOverlay::Slot()[SNew(SImage).Image(Br.Get())]
					];
			}
			else Avatar = SNew(SBox).WidthOverride(52.f).HeightOverride(56.f)[Portrait(R.bHasAvatar ? &R.Avatar : nullptr, 50.f)];
		}
		// puan bileşen çipleri
		TSharedRef<SHorizontalBox> PartsBox = SNew(SHorizontalBox);
		for (const TCHAR* K : { TEXT("match"), TEXT("pace"), TEXT("discipline"), TEXT("intercept"), TEXT("fpv") })
		{
			const int32* V = R.Parts.Find(K);
			if (!V) continue;
			const FLinearColor Pc = FCString::Strcmp(K, TEXT("fpv")) == 0 ? Hex(0xf472b6) : FCString::Strcmp(K, TEXT("intercept")) == 0 ? Danger() : FCString::Strcmp(K, TEXT("pace")) == 0 ? Accent3() : FCString::Strcmp(K, TEXT("discipline")) == 0 ? Warn() : Accent();
			PartsBox->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SBorder).BorderImage(Round(8.f)).BorderBackgroundColor(Pc.CopyWithNewOpacity(0.16f)).Padding(FMargin(8.f, 2.f))
				[
					SmvUi::Text(FText::FromString(FSmvI18n::T(FString(TEXT("top10.part_")) + K).ToString() + TEXT(" ") + FText::AsNumber(*V).ToString()), 10, "Bold", Pc, false)
				]
			];
		}
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
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)[Avatar]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(R.Name + (R.bDemo ? FSmvI18n::T(TEXT("top10.demo")).ToString() : FString())), 14, "Bold", Fg(), false)]
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(R.Email), 10, "Regular", Mute(), false)]
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FSmvI18n::Format(TEXT("top10.row"), { { TEXT("w"), FString::FromInt(R.Wins) }, { TEXT("m"), FString::FromInt(R.Matches) }, { TEXT("f"), FString::FromInt(R.Flights) } }), 11, "Regular", Dim(), false)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[PartsBox]
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
	TSharedRef<SVerticalBox> Top = SNew(SVerticalBox);
	Top->AddSlot().AutoHeight().Padding(0, 0, 0, 14)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[Tabs({ Tx(TEXT("ach.tab_flights")), Tx(TEXT("ach.tab")) }, { ESmvIcon::Book, ESmvIcon::Trophy }, &LogTab)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Replay).Text(Tx(TEXT("replay.open")))
			.OnClick([F = Flow]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Replay); })]
	];
	Top->AddSlot().AutoHeight()[LogTab == 1 ? Achievements() : AsW<SVerticalBox>(B)];
	return Page(Tx(TEXT("menu.logbook")), Tx(TEXT("menu.logbook_sub")), ESmvIcon::Replay, SNew(SBox).HAlign(HAlign_Left)[SNew(SBox).WidthOverride(1020.f)[Top]], true, true, true);
}

TSharedRef<SWidget> SSmvShell::Achievements()
{
	USmvGameInstance* GI = GameMode.IsValid() ? USmvGameInstance::GetSmv(GameMode.Get()) : nullptr;
	const USmvSteamSubsystem* St = GI ? GI->GetSubsystem<USmvSteamSubsystem>() : nullptr;
	static const TCHAR* Ids[] = { SmvAch::FirstFlight, SmvAch::FirstLanding, SmvAch::RingWin, SmvAch::DogfightWin, SmvAch::DisciplineA, SmvAch::AllRings,
		SmvAch::Interceptor, SmvAch::Ace, SmvAch::Ramp, SmvAch::Rover, SmvAch::Hours10, SmvAch::CloudFlight };
	static const ESmvIcon Icons[] = { ESmvIcon::Plane, ESmvIcon::Check, ESmvIcon::Flag, ESmvIcon::Match, ESmvIcon::Shield, ESmvIcon::Route,
		ESmvIcon::Bolt, ESmvIcon::Star, ESmvIcon::Rocket, ESmvIcon::Rover, ESmvIcon::Sun, ESmvIcon::Cloud };
	int32 Got = 0;
	for (const TCHAR* Id : Ids) if (St && St->IsUnlockedLocally(Id)) ++Got;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Section(Tx(TEXT("ach.steam")), Warn())]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FSmvI18n::Format(TEXT("ach.progress"), { { TEXT("n"), FString::FromInt(Got) }, { TEXT("t"), TEXT("12") } }), 12, "Bold", Warn(), false)]
	];
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(5.f));
	for (int32 i = 0; i < 12; ++i)
	{
		const bool bOn = St && St->IsUnlockedLocally(Ids[i]);
		const FLinearColor C = bOn ? Hex(0xfbbf24) : Mute();
		Grid->AddSlot(i % 3, i / 3)
		[
			SNew(SBorder).BorderImage(Round(14.f)).BorderBackgroundColor(bOn ? C.CopyWithNewOpacity(0.12f) : Card()).Padding(FMargin(14.f, 10.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
				[
					SNew(SBox).WidthOverride(42.f).HeightOverride(42.f)
					[
						SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(C.CopyWithNewOpacity(bOn ? 0.9f : 0.18f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							Icon(Icons[i], 22.f, bOn ? Hex(0x111827) : Mute())
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FSmvI18n::T(FString(TEXT("ach.")) + Ids[i]), 13, "Bold", bOn ? Fg() : Dim(), false)]
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FSmvI18n::T(FString(TEXT("ach.")) + Ids[i] + TEXT("_d")), 10, "Regular", Mute())]
				]
			]
		];
	}
	V->AddSlot().AutoHeight()[Grid];
	// rozetler: uçuş kayıtlarındaki sayılar
	TMap<FString, int32> Cnt;
	if (Data()) for (const FSmvFlightSummary& F : Data()->Flights()) for (const FString& Bd : F.Badges) Cnt.FindOrAdd(Bd)++;
	V->AddSlot().AutoHeight().Padding(0, 18, 0, 8)[Section(Tx(TEXT("ach.badges")), Accent())];
	TSharedRef<SUniformGridPanel> BadgeGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(5.f));
	int32 k = 0;
	for (const TCHAR* Bd : { TEXT("first"), TEXT("quick"), TEXT("ace"), TEXT("close"), TEXT("hunter"), TEXT("race_win"), TEXT("dogfight_win"), TEXT("fpv_pb") })
	{
		const int32 N = Cnt.FindRef(Bd);
		const FLinearColor C = N > 0 ? Accent() : Mute();
		BadgeGrid->AddSlot(k % 4, k / 4)
		[
			SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(N > 0 ? C.CopyWithNewOpacity(0.12f) : Card()).Padding(FMargin(12.f, 8.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Icon(ESmvIcon::Star, 18.f, C)]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FSmvI18n::T(FString(TEXT("badge.")) + Bd), 11, "Bold", N > 0 ? Fg() : Dim())]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[SmvUi::Text(N > 0 ? FSmvI18n::Format(TEXT("ach.count"), { { TEXT("n"), FString::FromInt(N) } }) : FText::FromString(TEXT("—")), 11, "Bold", C, false)]
			]
		];
		++k;
	}
	V->AddSlot().AutoHeight()[BadgeGrid];
	// savunma rütbeleri merdiveni
	V->AddSlot().AutoHeight().Padding(0, 18, 0, 6)[Section(Tx(TEXT("ach.ranks")), Danger())];
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[SmvUi::Text(Tx(TEXT("ach.ranks_note")), 11, "Regular", Dim())];
	TSharedRef<SHorizontalBox> Ladder = SNew(SHorizontalBox);
	static const TCHAR* RankId[] = { TEXT("private"), TEXT("corporal"), TEXT("sergeant"), TEXT("lieutenant"), TEXT("captain"), TEXT("major"), TEXT("colonel") };
	static const int32 RankPct[] = { 0, 25, 42, 58, 72, 85, 95 };
	for (int32 r = 0; r < 7; ++r)
	{
		const FLinearColor C = FLinearColor::LerpUsingHSV(Hex(0x94a3b8), Hex(0xfbbf24), r / 6.f);
		Ladder->AddSlot().FillWidth(1.f).Padding(0, 0, 6, 0).VAlign(VAlign_Bottom)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)[SmvUi::Text(FSmvI18n::T(FString(TEXT("site.rk_")) + RankId[r]), 11, "Bold", C, false)]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(18.f + r * 10.f)[SNew(SBorder).BorderImage(Round(6.f)).BorderBackgroundColor(C.CopyWithNewOpacity(0.35f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[SmvUi::Text(FText::FromString(FString::Printf(TEXT("≥ %d%%"), RankPct[r])), 10, "Bold", Fg(), false)]]
			]
		];
	}
	V->AddSlot().AutoHeight()[Ladder];
	return V;
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
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("gtheme.title")), Chips(&P.DialTheme, { TEXT("cockpit"), TEXT("classic"), TEXT("glass"), TEXT("minimal"), TEXT("military"), TEXT("none") },
		{ Tx(TEXT("gtheme.cockpit")), Tx(TEXT("gtheme.classic")), Tx(TEXT("gtheme.glass")), Tx(TEXT("gtheme.minimal")), Tx(TEXT("gtheme.military")), Tx(TEXT("gtheme.none")) }, {}, SaveP, 3), Tx(TEXT("gtheme.sub")))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.analog_fpv")), Toggle(&S.bAnalogFpv))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.leader_hidden")), Toggle(&S.bLeaderboardHidden))];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 8)[Section(Tx(TEXT("set.interface")), Accent2())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.theme")), Chips(&S.Theme, { TEXT("dark"), TEXT("light"), TEXT("auto") },
		{ Tx(TEXT("set.theme_dark")), Tx(TEXT("set.theme_light")), Tx(TEXT("set.theme_auto")) }, {}, [Self]() { ApplyTheme(Self->EditSettings.Theme); Self->Refresh(); }))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.reduced_motion")), Toggle(&S.bReducedMotion, [Self]() { ReducedMotion() = Self->EditSettings.bReducedMotion; }), Tx(TEXT("set.reduced_motion_hint")))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.menu_scene")), Toggle(&P.bMenuScene, SaveP), Tx(TEXT("set.menu_scene_hint")))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.preview3d")), Toggle(&P.bPreview3d, SaveP))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.season")), Chips(&P.Season, { TEXT("auto"), TEXT("off") }, { Tx(TEXT("set.season_auto")), Tx(TEXT("set.season_off")) }, {}, SaveP))];
	{
		static TArray<FString> Cc; static TArray<FText> CcL;
		if (Cc.Num() == 0) { Cc.Add(FString()); CcL.Add(Tx(TEXT("holiday.auto"))); for (const FString& C : SmvHoliday::Countries()) { Cc.Add(C); CcL.Add(FText::FromString(C)); } }
		V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("holiday.country")), Cycle(&P.HolidayCountry, Cc, CcL, [Self]() { FSmvUiPrefs::Get().Save(); Self->Refresh(); }, 220.f),
			FText::FromString(SmvHoliday::Country() + TEXT(" · ") + (SmvHoliday::Today(SmvHoliday::Country()).Num() ? SmvHoliday::Today(SmvHoliday::Country())[0].Name : FSmvI18n::T(TEXT("holiday.none")).ToString())))];
		static TArray<FString> Th; static TArray<FText> ThL;
		if (Th.Num() == 0) { Th.Add(FString()); ThL.Add(FText::FromString(TEXT("—"))); for (const FString& T : SmvHoliday::Themes()) { Th.Add(T); ThL.Add(FSmvI18n::T(TEXT("holiday.name_") + T)); } }
		V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("holiday.preview")), Cycle(&SmvHoliday::Preview(), Th, ThL, [Self]() { Self->Refresh(); }, 220.f))];
	}
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
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("rcfeel.all")), Toggle(&P.bLinkRP, [Self]() { FSmvUiPrefs::Get().SyncLink(); FSmvUiPrefs::Get().Save(); Self->Refresh(); }))];
			auto SaveL = []() { FSmvUiPrefs::Get().SyncLink(); FSmvUiPrefs::Get().Save(); };
			static const TCHAR* AxKey[4] = { TEXT("rcfeel.roll"), TEXT("rcfeel.pitch"), TEXT("rcfeel.thr"), TEXT("rcfeel.yaw") };
			for (int32 Ax : { 0, 1, 3, 2 })
			{
				if (Ax == 1 && P.bLinkRP) continue;
				const FText Name = (Ax == 0 && P.bLinkRP) ? Tx(TEXT("rcfeel.all")) : Tx(AxKey[Ax]);
				V->AddSlot().AutoHeight().Padding(0, 8, 0, 2)[SmvUi::Text(Name, 12, "Bold", Accent3())];
				V->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("rcfeel.rate")), SliderF(&P.Rate[Ax], 0.3f, 1.f, 0.05f, Pct, SaveL), Tx(TEXT("rcfeel.rate_hint")))];
				V->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("rcfeel.expo")), SliderF(&P.Ex[Ax], 0.f, 0.9f, 0.05f, Pct, SaveL), Tx(TEXT("rcfeel.expo_hint")))];
				V->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("rcfeel.dead")), SliderF(&P.Dead[Ax], 0.f, 0.2f, 0.01f, Pct, SaveL), Tx(TEXT("rcfeel.dead_hint")))];
			}
			V->AddSlot().AutoHeight().Padding(0, 4)[SmvUi::Text(Tx(TEXT("rcfeel.plane_thr")), 10, "Regular", Mute())];
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
			for (int32 Ax : { 0, 3, 2 })
			{
				TArray<FVector2D> P;
				for (int32 i = 0; i <= 40; ++i)
				{
					const float X = -1.f + 2.f * i / 40.f;
					const float Y = FSmvUiPrefs::Get().Shape(X, Ax, Fe);
					P.Add(FVector2D(Sz.X * (X + 1.f) * 0.5f, Sz.Y * (1.f - (Y + 1.f) * 0.5f)));
				}
				DrLine(Out, L + 2, G, P, Ax == 0 ? Accent() : Ax == 3 ? Warn() : Accent3(), 2.f);
			}
			DrText(Out, L + 3, G, TEXT("roll/pitch"), FVector2D(8.f, 12.f), Font(9, "Bold"), Accent(), -1);
			DrText(Out, L + 3, G, TEXT("yaw"), FVector2D(8.f, 26.f), Font(9, "Bold"), Warn(), -1);
			DrText(Out, L + 3, G, FSmvI18n::T(TEXT("rcfeel.thr")).ToString(), FVector2D(8.f, 40.f), Font(9, "Bold"), Accent3(), -1);
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
					.OnChanged([Self, i](bool b) { Self->EditSettings.RcInvert[i] = b; })]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
				[
					SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(11).Icon(ESmvIcon::Stick)
					.Text_Lambda([Self, i]() { return Self->DetectSlot == i ? FSmvI18n::T(TEXT("pad.move")) : FSmvI18n::T(TEXT("pad.learn")); })
					.Selected_Lambda([Self, i]() { return Self->DetectSlot == i; })
					.OnClick([Self, i]()
					{
						ASmvPilotPawn* Pw = Self->GameMode.IsValid() ? Self->GameMode->GetPilot() : nullptr;
						if (!Pw) return;
						Self->DetectSlot = i; Self->DetectAt = FPlatformTime::Seconds();
						for (int32 k = 0; k < 8; ++k) Self->DetectBase[k] = Pw->GetRawAxis(k);
					})
				])];
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
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("set.gpws_speech")), Toggle(&FSmvUiPrefs::Get().bGpwsSpeech, []() { FSmvUiPrefs::Get().Save(); }), Tx(TEXT("set.gpws_speech_hint")))];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Volume).Text(Tx(TEXT("set.alert_test"))).OnClick([Self = this]()
	{
		FSmvUiSound::Play(ESmvSnd::Error);
		const bool bAv = Self->EditSettings.GpwsLang != TEXT("ui");
		if (FSmvUiPrefs::Get().bGpwsSpeech) SmvSpeech::Say(bAv ? FString(TEXT("pull up")) : FSmvI18n::T(TEXT("gpws.c_PULL_UP")).ToString().ToLower(), bAv ? FString(TEXT("en")) : FSmvI18n::Language(), (float)Self->EditSettings.Volume, 500);
	})];
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

// ---------------------------------------------------------------------------
// "Nasıl uçmak istersin?" (web fly/index.js)
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::FlyMode()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	const bool bIn = Client() && Client()->IsLoggedIn();
	TSharedRef<SSmvButton> Solo = SNew(SSmvButton).Kind(ESmvBtn::Hero).Icon(ESmvIcon::Plane).Accent(Hex(0xfbbf24)).MinHeight(220.f)
		.Text(Tx(TEXT("fly.solo"))).SubText(Tx(TEXT("fly.solo_sub")))
		.OnClick([F]() { if (F.IsValid()) { F->NewDraft(); F->Race.Kind = TEXT("none"); F->SetScreen(ESmvScreen::Wizard); } });
	DefaultFocus = Solo;
	TSharedRef<SUniformGridPanel> G = SNew(SUniformGridPanel).SlotPadding(FMargin(8.f)).MinDesiredSlotHeight(220.f);
	G->AddSlot(0, 0)[Solo];
	G->AddSlot(1, 0)[SNew(SSmvButton).Kind(ESmvBtn::Tile).Icon(ESmvIcon::Fpv).Accent(Hex(0xf472b6)).MinHeight(220.f)
		.Text(Tx(TEXT("fly.fpv"))).SubText(Tx(TEXT("fly.fpv_sub"))).OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::FpvGallery); })];
	G->AddSlot(2, 0)[SNew(SSmvButton).Kind(ESmvBtn::Tile).Icon(ESmvIcon::Match).Accent(Hex(0x34d399)).MinHeight(220.f)
		.Text(Tx(TEXT("fly.multi"))).SubText(Tx(bIn ? TEXT("fly.multi_sub") : TEXT("fly.multi_login"))).OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Lobby); })];
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[G];
	// yapay zekâya karşı bandı
	V->AddSlot().AutoHeight().Padding(8, 8, 8, 0)
	[
		SNew(SBorder).BorderImage(Round(16.f)).BorderBackgroundColor(Accent2().CopyWithNewOpacity(0.12f)).Padding(FMargin(20.f, 14.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)[Icon(ESmvIcon::Drone, 34.f, Accent2())]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(Tx(TEXT("fly.ai")), 16, "Bold", Fg(), false)]
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(Tx(TEXT("fly.ai_sub")), 11, "Regular", Dim(), false)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Btn(Tx(TEXT("race.rings")), [F]() { if (F.IsValid()) { F->NewDraft(); F->Race.Kind = TEXT("rings"); F->SetScreen(ESmvScreen::Wizard); } }, false, true, ESmvIcon::Route)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Btn(Tx(TEXT("race.dogfight")), [F]() { if (F.IsValid()) { F->NewDraft(); F->Race.Kind = TEXT("dogfight"); F->Race.Bots = 1; F->SetScreen(ESmvScreen::Wizard); } }, false, true, ESmvIcon::Match)]
		]
	];
	// davet kodu
	V->AddSlot().AutoHeight().Padding(8, 12, 8, 0)
	[
		Glass(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)[SmvUi::Text(Tx(TEXT("fly.invite")), 13, "Bold", Fg(), false)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[TextEdit(&MatchCode, false, Tx(TEXT("lobby.code")), 160.f)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[TextEdit(&MatchPassword, true, Tx(TEXT("lobby.password")), 160.f)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Btn(Tx(TEXT("lobby.join_btn")), [F, Self]()
			{
				if (!F.IsValid()) return;
				if (!(Self->Client() && Self->Client()->IsLoggedIn())) { F->SetScreen(ESmvScreen::Login); return; }
				F->EnsureMatchClient();
				if (F->GetMatch()) { F->GetMatch()->Join(Self->MatchCode, Self->MatchPassword); F->SetScreen(ESmvScreen::Lobby); }
			}, false, true, ESmvIcon::Next)]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				F.IsValid() && F->GetMatch() && F->GetMatch()->HasMatch()
				? Btn(Tx(TEXT("fly.back_lobby")), [F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Lobby); }, true, true, ESmvIcon::Match)
				: SNullWidget::NullWidget
			], FMargin(20.f, 12.f))
	];
	return Page(Tx(TEXT("fly.title")), Tx(TEXT("fly.sub")), ESmvIcon::Play, V);
}

// ---------------------------------------------------------------------------
// hazır FPV parkurları (web fly/fpv.js galerisi)
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::FpvMap(const FString& CourseId, float Wd, float Ht)
{
	TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(Wd, Ht)).Visibility(EVisibility::HitTestInvisible);
	C->Painter = [CourseId](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FSmvFpvCourse* Co = SmvFpv::Find(CourseId);
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		DrBox(Out, L, G, FVector2D::ZeroVector, Sz, Hex(0x0b1424, 0.95f), Round(12.f));
		if (!Co) return;
		const double Span = FMath::Max(Co->Size.X, Co->Size.Y) * 1.15;
		const double K = FMath::Min(Sz.X, Sz.Y) / FMath::Max(Span, 10.0);
		const FVector2D Ctr = Sz * 0.5;
		auto M = [&](double X, double Y) { return Ctr + FVector2D(X * K, -Y * K); };
		Out.PushClip(FSlateClippingZone(G));
		for (const FSmvFpvObst& E : Co->Obstacles)
		{
			if (!E.bVis) continue;
			const FLinearColor Cl = SmvFpv::MatColor(E.Mat).CopyWithNewOpacity(0.55f);
			if (E.T == TEXT("box"))
			{
				const FVector R = SmvFpv::Right(E.Yaw) * (E.W * 0.5), F = SmvFpv::Forward(E.Yaw) * (E.D * 0.5);
				const FVector Cc(E.X, E.Y, 0);
				const FVector Pts[4] = { Cc - R - F, Cc + R - F, Cc + R + F, Cc - R + F };
				DrLine(Out, L + 1, G, { M(Pts[0].X, Pts[0].Y), M(Pts[1].X, Pts[1].Y), M(Pts[2].X, Pts[2].Y), M(Pts[3].X, Pts[3].Y), M(Pts[0].X, Pts[0].Y) }, Cl, 1.2f);
			}
			else if (E.T == TEXT("cyl") || E.T == TEXT("sph"))
			{
				const float Rr = FMath::Max(1.5f, (float)(E.R * K));
				DrBox(Out, L + 1, G, M(E.X, E.Y) - FVector2D(Rr, Rr), FVector2D(2 * Rr, 2 * Rr), Cl, Circle());
			}
			else if (E.T == TEXT("pipe")) DrSeg(Out, L + 1, G, M(E.A.X, E.A.Y), M(E.B.X, E.B.Y), Cl, FMath::Max(1.f, (float)(E.R * 2 * K)));
		}
		// yarış hattı (kapı merkezleri sırayla) + kapılar
		TArray<FVector2D> Line;
		for (const FSmvFpvGate& Gt : Co->Gates) Line.Add(M(Gt.X, Gt.Y));
		if (Line.Num()) Line.Add(Line[0]);
		DrLine(Out, L + 2, G, Line, Accent().CopyWithNewOpacity(0.35f), 1.5f);
		for (int32 i = 0; i < Co->Gates.Num(); ++i)
		{
			const FSmvFpvGate& Gt = Co->Gates[i];
			const FLinearColor Gc = i == 0 ? Accent3() : Hex(0xf472b6);
			const FVector R = SmvFpv::Right(Gt.Yaw) * (Gt.W * 0.5);
			const FVector Cc(Gt.X, Gt.Y, 0);
			DrSeg(Out, L + 3, G, M(Cc.X - R.X, Cc.Y - R.Y), M(Cc.X + R.X, Cc.Y + R.Y), Gc, 3.f);
			DrText(Out, L + 4, G, i == 0 ? FString(TEXT("S/F")) : FString::FromInt(i), M(Gt.X, Gt.Y) + FVector2D(0, -9), Font(8, "Bold"), Fg(), 0);
		}
		DrBox(Out, L + 3, G, M(Co->StartX, Co->StartY) - FVector2D(4, 4), FVector2D(8, 8), Warn(), Circle());
		Out.PopClip();
	};
	return C;
}

TSharedRef<SWidget> SSmvShell::FpvGallery()
{
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	const TArray<FSmvFpvCourse>& Cs = SmvFpv::Courses();
	if (Cs.Num() == 0) return Page(Tx(TEXT("fpv.gallery")), Tx(TEXT("fpv.tt_open_d")), ESmvIcon::Fpv, SmvUi::Text(Tx(TEXT("fpv.load_err")), 13, "Regular", Danger()));
	if (!SmvFpv::Find(FpvSel)) { FpvSel = Cs[0].Id; FpvEdit = FSmvFpvCfg(); FpvEdit.CourseId = FpvSel; FpvEdit.Laps = Cs[0].Laps; }
	const FSmvFpvCourse& Sel = *SmvFpv::Find(FpvSel);
	auto Stars = [](int32 D) { return FString::Printf(TEXT("%d/5"), D); };
	TSharedRef<SUniformGridPanel> G = SNew(SUniformGridPanel).SlotPadding(FMargin(6.f)).MinDesiredSlotWidth(250.f);
	for (int32 i = 0; i < Cs.Num(); ++i)
	{
		const FSmvFpvCourse& C = Cs[i];
		const FString Id = C.Id;
		const FSmvFpvRecord Rec = SmvFpv::LoadRecord(C.Id, C.Laps);
		const FString Sub = FString::Printf(TEXT("%s · %s\n%d %s · %d %s · %.0f m%s"), *C.VenueName().ToString(), *Stars(C.Difficulty), C.Laps, *Tx(TEXT("fpv.laps")).ToString(),
			C.GateCount, *Tx(TEXT("fpv.gates")).ToString(), C.LengthM, Rec.TotalS > 0 ? *FString::Printf(TEXT(" · %s %.2f s"), *Tx(TEXT("fpv.my_best")).ToString(), Rec.TotalS) : TEXT(""));
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Hex(0xf472b6)).Selected(C.Id == FpvSel).MinHeight(230.f)
			.Text(C.Name()).SubText(FText::FromString(Sub))
			.OnClick([Self, Id, C]() { Self->FpvSel = Id; Self->FpvEdit = FSmvFpvCfg(); Self->FpvEdit.CourseId = Id; Self->FpvEdit.Laps = C.Laps; Self->Refresh(); })
			.Extra()[SNew(SBox).WidthOverride(236.f).HeightOverride(110.f)[FpvMap(C.Id, 236.f, 110.f)]];
		if (C.Id == FpvSel) DefaultFocus = B;
		G->AddSlot(i % 2, i / 2)[B];
	}
	// sağ: ayrıntı + kurallar + araç
	FpvEdit.CourseId = Sel.Id;
	TArray<FString> Ids; TArray<FText> Ls;
	if (Data()) for (const FSmvDrone& D : Data()->Drones()) if (D.Type == TEXT("multirotor") || D.Type == TEXT("fpv") || D.Type == TEXT("heli")) { Ids.Add(D.Id); Ls.Add(FText::FromString(FString::Printf(TEXT("%s (%s)"), *D.Name, *FSmvI18n::T(FString(TEXT("type.")) + D.Type).ToString()))); }
	if (!Ids.Contains(FpvDroneId)) FpvDroneId = Ids.Num() ? Ids[0] : FString();
	const FSmvFpvRecord Rec = SmvFpv::LoadRecord(Sel.Id, FpvEdit.Laps);
	auto Sec = [](double V) { return FString::Printf(TEXT("%.0f s"), V); };
	TSharedRef<SVerticalBox> R = SNew(SVerticalBox);
	R->AddSlot().AutoHeight()[SNew(SBox).WidthOverride(520.f).HeightOverride(300.f)[FpvMap(Sel.Id, 520.f, 300.f)]];
	R->AddSlot().AutoHeight().Padding(0, 10, 0, 2)[Title(Sel.Name(), 22)];
	R->AddSlot().AutoHeight()[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %s %s · %.0f m · %d %s · %s: %s"), *Sel.VenueName().ToString(), *Tx(TEXT("fpv.difficulty")).ToString(),
		*Stars(Sel.Difficulty), Sel.LengthM, Sel.GateCount, *Tx(TEXT("fpv.gates")).ToString(), *Tx(TEXT("fpv.kinds")).ToString(),
		*FString::JoinBy(Sel.Kinds, TEXT(", "), [](const FString& K) { return FSmvI18n::T(FString(TEXT("fpv.k_")) + K).ToString(); }))), 11, "Regular", Dim())];
	R->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SmvUi::Text(Rec.TotalS > 0
		? FSmvI18n::Format(TEXT("fpv.pb_keep"), { { TEXT("t"), FString::Printf(TEXT("%.2f s"), Rec.TotalS) }, { TEXT("d"), Rec.Date } })
		: Tx(TEXT("fpv.no_record")), 12, "Bold", Warn())];
	R->AddSlot().AutoHeight().Padding(0, 12, 0, 6)[Section(Tx(TEXT("fpv.rules")), Hex(0xf472b6))];
	R->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("fpv.laps")), SliderI(&FpvEdit.Laps, 1, 10))];
	R->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("fpv.crash_rule")), Chips(&FpvEdit.CrashRule, { TEXT("penalty"), TEXT("reset"), TEXT("continue") },
		{ Tx(TEXT("fpv.rule_penalty")), Tx(TEXT("fpv.rule_reset")), Tx(TEXT("fpv.rule_continue")) }, {}, nullptr, 3))];
	R->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("fpv.crash_pen")), Slider(&FpvEdit.CrashPenS, 0, 30, 0.5, Sec))];
	R->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("fpv.miss_pen")), Slider(&FpvEdit.MissPenS, 0, 30, 0.5, Sec))];
	R->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("fpv.time_limit")), SliderI(&FpvEdit.TimeLimitMin, 2, 30, 1, [](double V) { return FString::Printf(TEXT("%d dk"), (int32)V); }))];
	R->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("fpv.ghost")), Toggle(&FpvEdit.bGhost), Tx(TEXT("fpv.ghost_d")))];
	R->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("fpv.vehicle")), Ids.Num() ? Cycle(&FpvDroneId, Ids, Ls, nullptr, 280.f) : SmvUi::Text(Tx(TEXT("fpv.type_warn")), 11, "Regular", Warn()))];
	R->AddSlot().AutoHeight().Padding(0, 14, 0, 0)
	[
		SNew(SSmvButton).Kind(ESmvBtn::Hero).Icon(ESmvIcon::Play).Accent(Hex(0xf472b6)).Text(Tx(TEXT("fpv.start_tt"))).IsEnabled(Ids.Num() > 0)
		.OnClick([F, Self]()
		{
			if (!F.IsValid() || !Self->Data()) return;
			const FSmvFpvCourse* C = SmvFpv::Find(Self->FpvSel);
			if (!C) return;
			F->NewDraft();
			for (const FSmvDrone& D : Self->Data()->Drones()) if (D.Id == Self->FpvDroneId) F->Draft.Drone = D;
			F->Draft.Backend = ESmvFlightBackend::LocalSitl;
			F->Draft.Track = USmvFlow::MakeFpvTrack(*C, F->Draft.Track);
			F->Race.Kind = TEXT("none");
			F->Fpv = Self->FpvEdit;
			F->Fpv.CourseId = C->Id;
			Self->SaveSettingsQuiet();
			F->StartFlight();
		})
	];
	return Page(Tx(TEXT("fpv.gallery")), Tx(TEXT("fpv.tt_open_d")), ESmvIcon::Fpv, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 20, 0)[G]
		+ SHorizontalBox::Slot().AutoWidth()[Glass(R, FMargin(20.f))]);
}

void SSmvShell::RepSelect(int32 I)
{
	RepSel = I;
	if (!RepList.IsValidIndex(I)) { RepFrames.Reset(); RepLoaded.Empty(); return; }
	if (RepLoaded == RepList[I].File) return;
	FSmvReplayInfo Info;
	if (SmvReplay::Load(RepList[I].File, RepReq, RepFrames, &Info)) { RepList[I] = Info; RepLoaded = Info.File; }
	else { RepFrames.Reset(); RepLoaded.Empty(); }
}

void SSmvShell::OpenReport(const FSmvFlightRequest& Req, const FSmvFlightSummary& Sum, const TArray<FSmvReplayFrame>& Frames)
{
	const FString Path = SmvReplay::WriteReport(Req, Sum, Frames);
	if (Path.IsEmpty()) { Toast(FSmvI18n::T(TEXT("replay.bad")).ToString(), 3); return; }
	FPlatformProcess::LaunchURL(*(FString(TEXT("file:///")) + Path.Replace(TEXT("\\"), TEXT("/"))), nullptr, nullptr);
	Toast(FSmvI18n::T(TEXT("report.opened")).ToString(), 6);
}

TSharedRef<SWidget> SSmvShell::RouteMap(const TArray<FSmvReplayFrame>* Frames, float W, float H)
{
	TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(W, H));
	C->Painter = [Frames](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		DrBox(Out, L, G, FVector2D::ZeroVector, Sz, Hex(0x0b1222, 0.92f), Round(14.f));
		for (int32 k = 1; k < 6; ++k)
		{
			DrSeg(Out, L + 1, G, FVector2D(Sz.X * k / 6.0, 0), FVector2D(Sz.X * k / 6.0, Sz.Y), FLinearColor(1, 1, 1, 0.04f), 1.f);
			DrSeg(Out, L + 1, G, FVector2D(0, Sz.Y * k / 6.0), FVector2D(Sz.X, Sz.Y * k / 6.0), FLinearColor(1, 1, 1, 0.04f), 1.f);
		}
		if (!Frames || Frames->Num() < 2) { DrText(Out, L + 2, G, FSmvI18n::T(TEXT("ops.no_data")).ToString(), Sz * 0.5, Font(11), Mute(), 0); return; }
		const TArray<FSmvReplayFrame>& F = *Frames;
		const double La0 = F[0].Lat, Lo0 = F[0].Lon, Kx = 111320.0 * FMath::Cos(FMath::DegreesToRadians(La0)), Ky = 110540.0;
		double X0 = 1e18, X1 = -1e18, Y0 = 1e18, Y1 = -1e18, MaxA = 1;
		for (const FSmvReplayFrame& Q : F)
		{
			const double X = (Q.Lon - Lo0) * Kx, Y = (Q.Lat - La0) * Ky;
			X0 = FMath::Min(X0, X); X1 = FMath::Max(X1, X); Y0 = FMath::Min(Y0, Y); Y1 = FMath::Max(Y1, Y); MaxA = FMath::Max(MaxA, Q.AltRel);
		}
		const double Span = FMath::Max3(X1 - X0, Y1 - Y0, 20.0), S = (FMath::Min(Sz.X, Sz.Y) - 36.0) / Span;
		const FVector2D Off((Sz.X - (X1 - X0) * S) * 0.5, (Sz.Y - (Y1 - Y0) * S) * 0.5);
		auto P = [&](const FSmvReplayFrame& Q) { return FVector2D(Off.X + ((Q.Lon - Lo0) * Kx - X0) * S, Sz.Y - Off.Y - ((Q.Lat - La0) * Ky - Y0) * S); };
		const int32 Step = FMath::Max(1, F.Num() / 600);
		for (int32 i = Step; i < F.Num(); i += Step)
		{
			const float K = (float)FMath::Clamp(F[i].AltRel / MaxA, 0.0, 1.0);
			DrSeg(Out, L + 2, G, P(F[i - Step]), P(F[i]), FLinearColor::LerpUsingHSV(Hex(0x38bdf8), Hex(0xfbbf24), K), 2.5f);
		}
		DrBox(Out, L + 3, G, P(F[0]) - FVector2D(6, 6), FVector2D(12, 12), Accent3(), Circle());
		DrBox(Out, L + 3, G, P(F.Last()) - FVector2D(6, 6), FVector2D(12, 12), Danger(), Circle());
		DrText(Out, L + 4, G, FString::Printf(TEXT("N ↑   %.0f m"), Span), FVector2D(Sz.X - 12, 16), Font(10, "Bold"), Dim(), 1);
		DrText(Out, L + 4, G, FString::Printf(TEXT("▲ %.0f m"), MaxA), FVector2D(12, 16), Font(10, "Bold"), Warn(), -1);
	};
	return C;
}

TSharedRef<SWidget> SSmvShell::ReplayScreen()
{
	SSmvShell* Self = this;
	TSharedRef<SVerticalBox> Lf = SNew(SVerticalBox);
	if (RepList.Num() == 0) Lf->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("replay.empty")), 13, "Regular", Mute())];
	for (int32 i = 0; i < RepList.Num(); ++i)
	{
		const FSmvReplayInfo& I = RepList[i];
		FString When = I.Created.Left(16).Replace(TEXT("T"), TEXT(" "));
		const FString Sub = FString::Printf(TEXT("%s · %02d:%02d · %.2f km"), *When, (int32)I.DurationS / 60, (int32)I.DurationS % 60, I.DistM / 1000.0);
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Normal).LeftAlign(true).MinWidth(380.f)
			.Icon(I.DroneType == TEXT("plane") ? ESmvIcon::Plane : I.DroneType == TEXT("rover") ? ESmvIcon::Rover : I.DroneType == TEXT("heli") ? ESmvIcon::Heli : ESmvIcon::Drone)
			.Text(FText::FromString(I.Drone + (I.Track.IsEmpty() ? FString() : TEXT(" — ") + I.Track))).SubText(FText::FromString(Sub))
			.Selected(i == RepSel).OnClick([Self, i]() { Self->RepSelect(i); Self->Refresh(); });
		if (i == RepSel) DefaultFocus = B;
		Lf->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[B];
	}
	TSharedRef<SVerticalBox> Rt = SNew(SVerticalBox);
	Rt->AddSlot().AutoHeight().HAlign(HAlign_Center)[RouteMap(&RepFrames, 520.f, 420.f)];
	if (RepList.IsValidIndex(RepSel))
	{
		const FSmvReplayInfo& I = RepList[RepSel];
		Rt->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %s · ▲ %.0f m · %d"), *FSmvI18n::T(TEXT("result.") + (I.Result.IsEmpty() ? FString(TEXT("ok")) : I.Result)).ToString(),
			*FString::Printf(TEXT("%.2f km"), I.DistM / 1000.0), I.MaxAltM, I.Frames)), 12, "Bold", Dim(), false)];
		Rt->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Row(Tx(TEXT("replay.speed")), Chips(&RepSpeed, { TEXT("0.5"), TEXT("1"), TEXT("2"), TEXT("4"), TEXT("8") },
			{ FText::FromString(TEXT("½×")), FText::FromString(TEXT("1×")), FText::FromString(TEXT("2×")), FText::FromString(TEXT("4×")), FText::FromString(TEXT("8×")) }, {}))];
		const FString File = I.File;
		Rt->AddSlot().AutoHeight().Padding(0, 14, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Play).Text(Tx(TEXT("replay.play")))
				.OnClick([Self, File]() { if (Self->Flow.IsValid()) Self->Flow->StartReplay(File, FCString::Atof(*Self->RepSpeed)); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[SNew(SSmvButton).Icon(ESmvIcon::Book).Text(Tx(TEXT("report.open")))
				.OnClick([Self]()
				{
					FSmvFlightSummary Sm;
					if (Self->RepList.IsValidIndex(Self->RepSel)) { Sm.DurationS = Self->RepList[Self->RepSel].DurationS; Sm.DistM = Self->RepList[Self->RepSel].DistM; Sm.Result = Self->RepList[Self->RepSel].Result; }
					Self->OpenReport(Self->RepReq, Sm, Self->RepFrames);
				})]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Trash).Text(Tx(TEXT("common.delete")))
				.OnClick([Self, File]()
				{
					Self->Confirm(FSmvI18n::T(TEXT("dlg.delete_title")), FSmvI18n::T(TEXT("ops.delete_q")), FSmvI18n::T(TEXT("dlg.yes_delete")), true, [Self, File]()
					{
						IFileManager::Get().Delete(*File);
						Self->RepList = SmvReplay::List(); Self->RepLoaded.Empty(); Self->RepSelect(FMath::Clamp(Self->RepSel, 0, FMath::Max(0, Self->RepList.Num() - 1)));
						Self->Refresh();
					});
				})]
		];
	}
	return Page(Tx(TEXT("replay.title")), Tx(TEXT("replay.sub")), ESmvIcon::Replay, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[Glass(Lf, FMargin(18.f))]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Rt, FMargin(18.f))]);
}

void SSmvShell::SupLoad()
{
	if (!Client() || !Client()->IsLoggedIn()) return;
	Sup.bLoading = true; Sup.Err.Empty();
	TWeakPtr<SSmvShell> W = StaticCastSharedRef<SSmvShell>(AsShared());
	Client()->Get(TEXT("/api/support"), FSmvRestDone::CreateLambda([W](int32 Status, const TSharedPtr<FJsonObject>& J, const FString& Code, const FString& Msg)
	{
		TSharedPtr<SSmvShell> S = W.Pin();
		if (!S.IsValid()) return;
		S->Sup.bLoading = false;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Status >= 200 && Status < 300 && J.IsValid() && J->TryGetArrayField(TEXT("tickets"), Arr)) S->Sup.Tickets = *Arr;
		else S->Sup.Err = Status == 503 ? FSmvI18n::T(TEXT("support.unavailable")).ToString() : (Msg.IsEmpty() ? Code : Msg);
		S->Refresh();
	}));
}

void SSmvShell::SupOpen(const FString& Id)
{
	if (!Client()) return;
	Sup.OpenId = Id; Sup.Detail.Reset(); Sup.bLoading = true; Sup.Reply.Empty();
	TWeakPtr<SSmvShell> W = StaticCastSharedRef<SSmvShell>(AsShared());
	Client()->Get(TEXT("/api/support/") + FGenericPlatformHttp::UrlEncode(Id), FSmvRestDone::CreateLambda([W](int32 Status, const TSharedPtr<FJsonObject>& J, const FString& Code, const FString& Msg)
	{
		TSharedPtr<SSmvShell> S = W.Pin();
		if (!S.IsValid()) return;
		S->Sup.bLoading = false;
		if (Status >= 200 && Status < 300 && J.IsValid()) S->Sup.Detail = J;
		else S->Toast(Status == 503 ? FSmvI18n::T(TEXT("support.unavailable")).ToString() : (Msg.IsEmpty() ? Code : Msg), 3);
		S->Refresh();
	}));
}

void SSmvShell::SupSend()
{
	if (!Client() || Sup.bSending) return;
	const FString Subj = Sup.Subject.TrimStartAndEnd().Left(120), Body = Sup.Body.TrimStartAndEnd().Left(4000);
	if (Subj.IsEmpty() || Body.IsEmpty()) { Toast(FSmvI18n::T(Subj.IsEmpty() ? TEXT("support.subject_ph") : TEXT("support.body_ph")).ToString(), 4); return; }
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("category"), Sup.Cat);
	O->SetStringField(TEXT("subject"), Subj);
	O->SetStringField(TEXT("body"), Body);
	O->SetBoolField(TEXT("include_context"), Sup.bCtx);
	// ekran görüntüsü (PNG ≤ 3 MB → data URL)
	TArray<uint8> Png;
	if (!Sup.ShotPath.IsEmpty() && FFileHelper::LoadFileToArray(Png, *Sup.ShotPath) && Png.Num() > 0)
	{
		if (Png.Num() > 3 * 1024 * 1024) { Toast(FSmvI18n::T(TEXT("support.shot_toolarge")).ToString(), 3); return; }
		O->SetStringField(TEXT("screenshot"), TEXT("data:image/png;base64,") + FBase64::Encode(Png));
		O->SetStringField(TEXT("attach_name"), TEXT("caysim.png"));
	}
	if (Sup.bCtx)
	{
		TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
		FVector2D Vs(1920, 1080);
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Vs);
		C->SetStringField(TEXT("ua"), FString::Printf(TEXT("CAYSIM (Unreal Engine %s; %s %s)"), *FEngineVersion::Current().ToString(EVersionComponent::Patch), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()), *FPlatformMisc::GetOSVersion()).Left(300));
		C->SetStringField(TEXT("lang"), FSmvI18n::Language());
		C->SetStringField(TEXT("screen"), FString::Printf(TEXT("%.0fx%.0f@1.0"), Vs.X, Vs.Y));
		C->SetStringField(TEXT("gl"), FPlatformMisc::GetPrimaryGPUBrand().Left(200));
		if (Data() && Data()->Flights().Num()) C->SetStringField(TEXT("flight_id"), Data()->Flights()[0].FlightId); else C->SetField(TEXT("flight_id"), MakeShared<FJsonValueNull>());
		C->SetStringField(TEXT("route"), TEXT("caysim/support"));
		O->SetObjectField(TEXT("context"), C);
	}
	Sup.bSending = true;
	TWeakPtr<SSmvShell> W = StaticCastSharedRef<SSmvShell>(AsShared());
	Client()->Post(TEXT("/api/support"), O, FSmvRestDone::CreateLambda([W](int32 Status, const TSharedPtr<FJsonObject>& J, const FString& Code, const FString& Msg)
	{
		TSharedPtr<SSmvShell> S = W.Pin();
		if (!S.IsValid()) return;
		S->Sup.bSending = false;
		if (Status >= 200 && Status < 300 && J.IsValid())
		{
			const FString Ref = J->HasField(TEXT("code")) ? J->GetStringField(TEXT("code")) : FString::Printf(TEXT("#%d"), (int32)J->GetNumberField(TEXT("id")));
			S->Toast(FSmvI18n::T(TEXT("support.done_title")).ToString() + TEXT(" — ") + Ref, 6);
			FSmvUiSound::Play(ESmvSnd::Open);
			S->Sup.Subject.Empty(); S->Sup.Body.Empty(); S->Sup.ShotPath.Empty();
			S->SupLoad();
		}
		else S->Toast(Status == 429 ? FSmvI18n::T(TEXT("support.rate")).ToString() : Status == 503 ? FSmvI18n::T(TEXT("support.unavailable")).ToString() : (Msg.IsEmpty() ? Code : Msg), 3);
		S->Refresh();
	}));
}

void SSmvShell::SupReplySend()
{
	if (!Client() || Sup.OpenId.IsEmpty() || Sup.bSending) return;
	const FString V = Sup.Reply.TrimStartAndEnd().Left(4000);
	if (V.IsEmpty()) return;
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("body"), V);
	Sup.bSending = true;
	TWeakPtr<SSmvShell> W = StaticCastSharedRef<SSmvShell>(AsShared());
	Client()->Post(TEXT("/api/support/") + FGenericPlatformHttp::UrlEncode(Sup.OpenId) + TEXT("/reply"), O, FSmvRestDone::CreateLambda([W](int32 Status, const TSharedPtr<FJsonObject>& J, const FString& Code, const FString& Msg)
	{
		TSharedPtr<SSmvShell> S = W.Pin();
		if (!S.IsValid()) return;
		S->Sup.bSending = false;
		if (Status >= 200 && Status < 300 && J.IsValid()) { S->Sup.Detail = J; S->Sup.Reply.Empty(); S->Toast(FSmvI18n::T(TEXT("support.sent")).ToString(), 6); }
		else S->Toast(Status == 429 ? FSmvI18n::T(TEXT("support.rate")).ToString() : (Msg.IsEmpty() ? Code : Msg), 3);
		S->Refresh();
	}));
}

TSharedRef<SWidget> SSmvShell::Support()
{
	SSmvShell* Self = this;
	const bool bIn = Client() && Client()->IsLoggedIn();
	if (!bIn)
	{
		TWeakObjectPtr<USmvFlow> F = Flow;
		return Page(Tx(TEXT("support.title")), Tx(TEXT("support.sub")), ESmvIcon::Help, Glass(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(Tx(TEXT("support.login_needed")), 14, "Regular", Fg())]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 16, 0, 0)[Btn(Tx(TEXT("welcome.login")), [F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Login); }, true, true, ESmvIcon::User)], FMargin(24.f)));
	}
	auto StatusChip = [](const FString& St) -> TSharedRef<SWidget>
	{
		const FLinearColor C = St == TEXT("cozuldu") ? Accent3() : St == TEXT("kapali") ? Mute() : St == TEXT("yanit_bekliyor") ? Warn() : Accent();
		const FText L = FSmvI18n::T(TEXT("support.st_") + St);
		return SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(C.CopyWithNewOpacity(0.18f)).Padding(FMargin(10.f, 3.f))[SmvUi::Text(L, 11, "Bold", C, false)];
	};
	// ---- sol: sorun bildir ----
	TSharedRef<SVerticalBox> Lf = SNew(SVerticalBox);
	Lf->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("support.report")), Accent())];
	Lf->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[SmvUi::Text(Tx(TEXT("support.report_sub")), 11, "Regular", Dim())];
	Lf->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[SmvUi::Text(Tx(TEXT("support.category")), 11, "Bold", Dim(), false)];
	{
		TArray<FString> Cats = { TEXT("flight"), TEXT("account"), TEXT("hangar"), TEXT("track"), TEXT("mp"), TEXT("perf"), TEXT("other") };
		TArray<FText> Labels;
		for (const FString& C : Cats) Labels.Add(FSmvI18n::T(TEXT("support.cat_") + C));
		Lf->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Chips(&Sup.Cat, Cats, Labels, {}, nullptr, 3)];
	}
	Lf->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[SmvUi::Text(Tx(TEXT("support.subject")), 11, "Bold", Dim(), false)];
	Lf->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[TextEdit(&Sup.Subject, false, Tx(TEXT("support.subject_ph")), 440.f)];
	Lf->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[SmvUi::Text(Tx(TEXT("support.body")), 11, "Bold", Dim(), false)];
	Lf->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
	[
		SNew(SBox).MinDesiredHeight(130.f).WidthOverride(440.f)
		[
			SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Hex(0x0b1222, 0.92f)).Padding(FMargin(10.f, 8.f))
			[
				SNew(SMultiLineEditableText).Font(Font(12)).AutoWrapText(true).Text(FText::FromString(Sup.Body)).HintText(Tx(TEXT("support.body_ph")))
				.OnTextChanged_Lambda([Self](const FText& T) { Self->Sup.Body = T.ToString().Left(4000); })
			]
		]
	];
	Lf->AddSlot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("support.ctx")), Toggle(&Sup.bCtx))];
	Lf->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[SmvUi::Text(Tx(TEXT("support.shot")), 11, "Bold", Dim(), false)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(11).Icon(ESmvIcon::Camera).Text(Tx(TEXT("support.shot_auto")))
			.OnClick([Self]()
			{
				// menü katmanıyla birlikte sahne; dosya bir sonraki karede yazılır
				Self->Sup.ShotPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Simumav/support_shot.png"));
				IFileManager::Get().Delete(*Self->Sup.ShotPath, false, true, true);
				FScreenshotRequest::RequestScreenshot(Self->Sup.ShotPath, true, false);
				Self->Toast(FSmvI18n::T(TEXT("support.shot_taken")).ToString(), 6);
				Self->Refresh();
			})]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(11).Icon(ESmvIcon::Trash).Text(Tx(TEXT("support.shot_remove")))
			.Visibility(Sup.ShotPath.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			.OnClick([Self]() { Self->Sup.ShotPath.Empty(); Self->Refresh(); })]
	];
	Lf->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(Sup.ShotPath.IsEmpty() ? Tx(TEXT("support.shot_none")) : FText::FromString(FPaths::GetCleanFilename(Sup.ShotPath)), 10, "Regular", Mute())];
	TSharedRef<SSmvButton> SendBtn = SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Next).MinWidth(200.f).Text(Tx(TEXT("support.send")))
		.IsEnabled_Lambda([Self]() { return !Self->Sup.bSending; }).OnClick([Self]() { Self->SupSend(); });
	Lf->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 16, 0, 0)[SendBtn];

	// ---- sağ: taleplerim / ayrıntı ----
	TSharedRef<SVerticalBox> Rt = SNew(SVerticalBox);
	if (!Sup.OpenId.IsEmpty())
	{
		Rt->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 10)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(11).Text(Tx(TEXT("support.back_list")))
			.OnClick([Self]() { Self->Sup.OpenId.Empty(); Self->Sup.Detail.Reset(); Self->SupLoad(); Self->Refresh(); })];
		if (!Sup.Detail.IsValid()) Rt->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("support.loading")), 12, "Regular", Mute())];
		else
		{
			const TSharedPtr<FJsonObject>& D = Sup.Detail;
			const FString St = SmvJson::Str(D, TEXT("status"));
			const bool bClosed = St == TEXT("kapali") || St == TEXT("cozuldu");
			const FString Code = SmvJson::Str(D, TEXT("code"));
			Rt->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[SmvUi::Text(FText::FromString(Code.IsEmpty() ? TEXT("#") + Sup.OpenId : Code), 12, "Bold", Accent(), false)]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(SmvJson::Str(D, TEXT("subject"))), 14, "Bold", Fg())]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StatusChip(St)]
			];
			const TArray<TSharedPtr<FJsonValue>>* Msgs = nullptr;
			if (D->TryGetArrayField(TEXT("messages"), Msgs))
			{
				for (const TSharedPtr<FJsonValue>& Mv : *Msgs)
				{
					const TSharedPtr<FJsonObject> Mo = Mv.IsValid() ? Mv->AsObject() : nullptr;
					if (!Mo.IsValid()) continue;
					const FString Kind = SmvJson::Str(Mo, TEXT("author_kind"));
					const bool bMe = Kind == TEXT("company"), bSys = Kind == TEXT("system");
					const FString Who = bMe ? FSmvI18n::T(TEXT("support.you")).ToString() : bSys ? FSmvI18n::T(TEXT("support.system")).ToString()
						: (SmvJson::Str(Mo, TEXT("author_name")).IsEmpty() ? FSmvI18n::T(TEXT("support.team")).ToString() : SmvJson::Str(Mo, TEXT("author_name")));
					Rt->AddSlot().AutoHeight().HAlign(bMe ? HAlign_Right : HAlign_Left).Padding(bMe ? 60.f : 0.f, 0.f, bMe ? 0.f : 60.f, 8.f)
					[
						SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(bMe ? Accent().CopyWithNewOpacity(0.16f) : bSys ? FLinearColor(1, 1, 1, 0.04f) : Accent2().CopyWithNewOpacity(0.14f)).Padding(FMargin(12.f, 8.f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(SmvJson::Str(Mo, TEXT("body"))), 12, "Regular", Fg())]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(FText::FromString(Who + TEXT(" · ") + SmvJson::Str(Mo, TEXT("created_at")).Left(16).Replace(TEXT("T"), TEXT(" "))), 10, "Regular", Mute(), false)]
						]
					];
				}
			}
			if (bClosed) Rt->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SmvUi::Text(Tx(TEXT("support.closed_note")), 11, "Regular", Mute())];
			else
			{
				Rt->AddSlot().AutoHeight().Padding(0, 8, 0, 6)[TextEdit(&Sup.Reply, false, Tx(TEXT("support.reply_ph")), 420.f)];
				Rt->AddSlot().AutoHeight().HAlign(HAlign_Left)[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Next).Text(Tx(TEXT("support.reply_send")))
					.IsEnabled_Lambda([Self]() { return !Self->Sup.bSending; }).OnClick([Self]() { Self->SupReplySend(); })];
			}
		}
	}
	else
	{
		Rt->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("support.my_tickets")), Accent2())];
		Rt->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[SmvUi::Text(Tx(TEXT("support.my_sub")), 11, "Regular", Dim())];
		if (Sup.bLoading && Sup.Tickets.Num() == 0) Rt->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("support.loading")), 12, "Regular", Mute())];
		else if (!Sup.Err.IsEmpty()) Rt->AddSlot().AutoHeight()[SmvUi::Text(FText::FromString(Sup.Err), 12, "Regular", Danger())];
		else if (Sup.Tickets.Num() == 0) Rt->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("support.empty")), 12, "Regular", Mute())];
		for (const TSharedPtr<FJsonValue>& Tv : Sup.Tickets)
		{
			const TSharedPtr<FJsonObject> To = Tv.IsValid() ? Tv->AsObject() : nullptr;
			if (!To.IsValid()) continue;
			const FString Id = To->HasTypedField<EJson::Number>(TEXT("id")) ? FString::FromInt((int32)To->GetNumberField(TEXT("id"))) : SmvJson::Str(To, TEXT("id"));
			const FString Code = SmvJson::Str(To, TEXT("code"));
			bool bUnread = false; To->TryGetBoolField(TEXT("unread"), bUnread);
			const FString Sub = FSmvI18n::T(TEXT("support.updated")).ToString() + TEXT(": ") + SmvJson::Str(To, TEXT("updated_at")).Left(16).Replace(TEXT("T"), TEXT(" "))
				+ (bUnread ? TEXT(" · ") + FSmvI18n::T(TEXT("support.unread")).ToString() : FString());
			Rt->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SSmvButton).Kind(ESmvBtn::Normal).LeftAlign(true).Icon(bUnread ? ESmvIcon::Bolt : ESmvIcon::Info)
				.Text(FText::FromString((Code.IsEmpty() ? TEXT("#") + Id : Code) + TEXT("  ") + SmvJson::Str(To, TEXT("subject"))))
				.SubText(FText::FromString(Sub + TEXT(" · ") + FSmvI18n::T(TEXT("support.st_") + SmvJson::Str(To, TEXT("status"))).ToString()))
				.Accent(bUnread ? Warn() : Accent())
				.OnClick([Self, Id]() { Self->SupOpen(Id); Self->Refresh(); })
			];
		}
	}
	return Page(Tx(TEXT("support.title")), Tx(TEXT("support.sub")), ESmvIcon::Help, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)[Glass(Lf, FMargin(22.f))]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Glass(Rt, FMargin(22.f))]);
}

#undef LOCTEXT_NAMESPACE
