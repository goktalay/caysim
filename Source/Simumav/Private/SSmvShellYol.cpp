// Simumav arayüzü — kullanıcı yol haritası ekranları (docs/yol-haritasi.md): sade ana menü (Hangar · Parkur · Mürettebat · Uçuş) + sağ üstte
// Pilot menüsü; Hangar ("+ Yeni" sihirbazı: tip → model 3B → parametreler → kamera → eklentiler → görünüm), Parkur (Karşılaşma / Özel / Gerçek
// Dünya), Mürettebat (pilot değiştir, seviye 1–100, XP, madalyalar; yeni pilot sihirbazı), Uçuş akışı (dron → parkur → oyun türü → tek /
// çok oyunculu → UÇ), uçuş sonrası deneyim ekranı, Arkadaşlarım. Arka planlar GERÇEK 3B sahne: menü sahnesinin kamera istasyonları
// (ASmvMenuScene::SetStation) — vitrin aracı / pilot gerçek dünyada, Lumen ışığında; düz degrade yok. Eski ekranlar Pilot menüsünden açılır.
#include "SSmvShell.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SmvDataService.h"
#include "SmvLocalFlightLink.h"
#include "SmvMatchClient.h"
#include "SmvGameMode.h"
#include "SmvGameInstance.h"
#include "SmvSteam.h"
#include "SmvFabContent.h"
#include "SmvMenuScene.h"
#include "SmvUiPrefs.h"
#include "SmvI18n.h"
#include "SmvReplay.h"
#include "SmvTileMap.h"
#include "SmvProgress.h"
#include "SmvGameRules.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <commdlg.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

using namespace SmvUi;

namespace
{
	template<typename T> TSharedRef<SWidget> YhW(TSharedRef<T> W) { return W; }

	FLinearColor YhHex(const FString& H, const FLinearColor& Def = FLinearColor::Gray)
	{
		return H.Len() == 7 ? FLinearColor::FromSRGBColor(FColor::FromHex(H)) : Def;
	}

	FText YhT(const TCHAR* Key) { return FSmvI18n::T(Key); }
	FText YhT(const FString& Key) { return FSmvI18n::T(Key); }

	const TArray<FString>& YhPalette()
	{
		static const TArray<FString> P = { TEXT("#e53935"), TEXT("#fb8c00"), TEXT("#fdd835"), TEXT("#43a047"), TEXT("#00acc1"), TEXT("#1e88e5"),
			TEXT("#5e35b1"), TEXT("#d81b60"), TEXT("#eceff1"), TEXT("#263238"), TEXT("#5d4037"), TEXT("#78909c") };
		return P;
	}

	/** Yol haritası araç sınıfı: multi (çok motorlu: multirotor + fpv + heli) | plane | vtol (vtol + interceptor) | rover */
	FString YhKindOf(const FString& Type)
	{
		if (Type == TEXT("plane")) return TEXT("plane");
		if (Type == TEXT("vtol") || Type == TEXT("interceptor")) return TEXT("vtol");
		if (Type == TEXT("rover")) return TEXT("rover");
		return TEXT("multi");
	}

	bool YhTypeInKind(const FString& Type, const FString& Kind) { return YhKindOf(Type) == Kind; }

	/** Model grubu (çok motorluda pervane sayısı): p4 | p6 | p8 | heli | fpv; diğer sınıflarda tip */
	FString YhGroupOf(const FSmvCatalogModel& M)
	{
		if (M.Type == TEXT("heli")) return TEXT("heli");
		if (M.Type == TEXT("multirotor") || M.Type == TEXT("fpv"))
		{
			int32 P = M.Motors > 0 ? M.Motors : 4;
			if (M.Key.Contains(TEXT("y6"))) P = 6;
			if (M.Key.Contains(TEXT("octa")) || M.Key.Contains(TEXT("x8"))) P = 8;
			if (M.Key.Contains(TEXT("typhoon")) || M.Key.Contains(TEXT("hexa"))) P = 6;
			return P >= 8 ? TEXT("p8") : P >= 6 ? TEXT("p6") : TEXT("p4");
		}
		return M.Type;
	}

	const FSlateBrush* YhRtBrush(TArray<TSharedPtr<FSlateBrush>>& Keep, UTextureRenderTarget2D* Rt, const FVector2D& Size)
	{
		if (!Rt) return nullptr;
		for (const TSharedPtr<FSlateBrush>& B : Keep) if (B.IsValid() && B->GetResourceObject() == Rt) return B.Get();
		TSharedPtr<FSlateBrush> B = MakeShared<FSlateBrush>();
		B->DrawAs = ESlateBrushDrawType::Image;
		B->SetResourceObject(Rt);
		B->ImageSize = Size;
		Keep.Add(B);
		return B.Get();
	}

	/** Harita önizleme görseli (kayıt tablosu PNG'si) */
	const FSlateBrush* YhMapPreview(const FSmvFabMap& M)
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

	/** Dosyadan PNG seç (Windows ortak iletişim kutusu; diğer platformlarda yok) */
	bool YhPickPng(FString& Out)
	{
#if PLATFORM_WINDOWS
		TCHAR Buf[1024] = { 0 };
		OPENFILENAMEW Ofn;
		FMemory::Memzero(&Ofn, sizeof(Ofn));
		Ofn.lStructSize = sizeof(Ofn);
		Ofn.hwndOwner = nullptr;
		Ofn.lpstrFilter = TEXT("PNG\0*.png\0\0");
		Ofn.lpstrFile = Buf;
		Ofn.nMaxFile = 1024;
		Ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetOpenFileNameW(&Ofn))
		{
			Out = FString(Buf);
			return true;
		}
#endif
		return false;
	}

	/** Seçilen PNG profil klasörüne kopyalanır (Saved/Simumav/Decals/<guid>.png); dönüş: yeni yol */
	FString YhImportDecal(const FString& Src)
	{
		TArray<uint8> B;
		if (!FFileHelper::LoadFileToArray(B, *Src) || B.Num() < 8 || B.Num() > 4 * 1024 * 1024) return FString();
		const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("Decals"));
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Dst = FPaths::ConvertRelativePathToFull(FPaths::Combine(Dir, FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".png")));
		return FFileHelper::SaveArrayToFile(B, *Dst) ? Dst : FString();
	}

	/** Tür (yol haritası "Uçuş › Oyun türü") simgesi ve vurgu rengi */
	ESmvIcon YhModeIcon(const FString& M)
	{
		if (M == TEXT("rings")) return ESmvIcon::Route;
		if (M == TEXT("cargo_drop")) return ESmvIcon::Next;
		if (M == TEXT("spraying")) return ESmvIcon::Rain;
		if (M == TEXT("dogfight")) return ESmvIcon::Match;
		if (M == TEXT("facility_defense")) return ESmvIcon::Shield;
		if (M == TEXT("ship_defense")) return ESmvIcon::Flag;
		return ESmvIcon::Plane;
	}
	uint32 YhModeColor(const FString& M)
	{
		if (M == TEXT("rings")) return 0xf472b6;
		if (M == TEXT("cargo_drop")) return 0xfbbf24;
		if (M == TEXT("spraying")) return 0x34d399;
		if (M == TEXT("dogfight")) return 0xf87171;
		if (M == TEXT("facility_defense")) return 0xa78bfa;
		if (M == TEXT("ship_defense")) return 0x60a5fa;
		return 0x38bdf8;
	}
	const TArray<FString>& YhModes()
	{
		static const TArray<FString> M = { TEXT("free"), TEXT("rings"), TEXT("cargo_drop"), TEXT("spraying"), TEXT("dogfight"), TEXT("facility_defense"), TEXT("ship_defense") };
		return M;
	}

	/** Medal rengi (bronz → efsane) */
	FLinearColor YhMedalColor(int32 Level)
	{
		if (Level <= 5) return Hex(0xcd7f32);
		if (Level <= 10) return Hex(0xc0c7d0);
		if (Level <= 25) return Hex(0xfbbf24);
		if (Level <= 50) return Hex(0xa5f3fc);
		if (Level <= 75) return Hex(0x93c5fd);
		return Hex(0xf472b6);
	}
}

// ---------------------------------------------------------------------------
// ana menü girişi: büyük, harf aralıklı başlık + ince alt yazı; üzerine gelince vurgu çizgisi uzar, metin kayar, sıra numarası parlar
// ---------------------------------------------------------------------------

class SSmvMenuEntry : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvMenuEntry) : _Index(1), _Primary(false), _Delay(0.f), _Accent(FLinearColor(0.22f, 0.74f, 0.97f)) {}
		SLATE_ARGUMENT(FText, Title)
		SLATE_ATTRIBUTE(FText, Sub)
		SLATE_ARGUMENT(int32, Index)
		SLATE_ARGUMENT(bool, Primary)
		SLATE_ARGUMENT(float, Delay)
		SLATE_ARGUMENT(FLinearColor, Accent)
		SLATE_ARGUMENT(TFunction<void()>, OnClick)
	SLATE_END_ARGS()

	void Construct(const FArguments& A)
	{
		Title = A._Title; Sub = A._Sub; Index = A._Index; bPrimary = A._Primary; Delay = A._Delay; AccentC = A._Accent; OnClick = A._OnClick;
		Born = FPlatformTime::Seconds();
	}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(620.f, bPrimary ? 112.f : 88.f); }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& G, const double Now, const float Dt) override
	{
		HoverA = FMath::FInterpTo(HoverA, (bHover || bFocus) ? 1.f : 0.f, Dt, 9.f);
		PressA = FMath::FInterpTo(PressA, bPressed ? 1.f : 0.f, Dt, 18.f);
	}
	virtual void OnMouseEnter(const FGeometry& G, const FPointerEvent& E) override { bHover = true; FSmvUiSound::Play(ESmvSnd::Hover); }
	virtual void OnMouseLeave(const FPointerEvent& E) override { bHover = false; bPressed = false; }
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
	{
		if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
		bPressed = true;
		return FReply::Handled();
	}
	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override
	{
		if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
		const bool bWas = bPressed;
		bPressed = false;
		if (bWas && G.IsUnderLocation(E.GetScreenSpacePosition())) Click();
		return FReply::Handled();
	}
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override
	{
		const FKey K = E.GetKey();
		if (K == EKeys::Enter || K == EKeys::SpaceBar || K == EKeys::Virtual_Accept || K == EKeys::Gamepad_FaceButton_Bottom) { Click(); return FReply::Handled(); }
		return FReply::Unhandled();
	}
	virtual FReply OnFocusReceived(const FGeometry& G, const FFocusEvent& E) override { bFocus = E.GetCause() != EFocusCause::Mouse; if (bFocus) FSmvUiSound::Play(ESmvSnd::Hover); return FReply::Handled(); }
	virtual void OnFocusLost(const FFocusEvent& E) override { bFocus = false; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 L,
		const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const FVector2D S = FVector2D(G.GetLocalSize());
		const float In = ReducedMotion() ? 1.f : FMath::Clamp((float)(FPlatformTime::Seconds() - Born - Delay) / 0.55f, 0.f, 1.f);
		const float E = 1.f - FMath::Pow(1.f - In, 3.f);
		const float H = HoverA;
		const float Ox = (1.f - E) * -40.f + H * 14.f - PressA * 4.f;
		const FLinearColor Ac = AccentC;
		// arka plan: üzerine gelince soldan sağa sönen ince cam şerit
		if (H > 0.01f)
			DrGradient(Out, L, G, FVector2D(0, 4), FVector2D(S.X, S.Y - 8), { { 0.f, FLinearColor(0.02f, 0.04f, 0.08f, 0.55f * H) }, { 1.f, FLinearColor(0.02f, 0.04f, 0.08f, 0.f) } }, false);
		// sıra numarası
		const FSlateFontInfo Fi = Font(bPrimary ? 15 : 13, "Bold", 200);
		DrText(Out, L + 1, G, FString::Printf(TEXT("%02d"), Index), FVector2D(18.f + Ox, S.Y * 0.5f - (bPrimary ? 16.f : 13.f)), Fi, (bPrimary ? Ac : FLinearColor(1, 1, 1, 0.45f + 0.55f * H)).CopyWithNewOpacity(E * (0.55f + 0.45f * H)), -1);
		// başlık
		const FSlateFontInfo Ft = Font(bPrimary ? 44 : 34, bPrimary ? "Black" : "Bold", 140);
		const FString T = Title.ToString().ToUpper();
		const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D Ts = M->Measure(T, Ft);
		const float Tx = 64.f + Ox;
		const float Ty = S.Y * 0.5f - (bPrimary ? 14.f : 11.f);
		DrText(Out, L + 2, G, T, FVector2D(Tx + 1.5f, Ty + 2.f), Ft, FLinearColor(0, 0, 0, 0.45f * E), -1);
		DrText(Out, L + 3, G, T, FVector2D(Tx, Ty), Ft, (bPrimary ? FLinearColor::LerpUsingHSV(FLinearColor::White, Ac, 0.25f + 0.5f * H) : FLinearColor(1, 1, 1, 0.82f + 0.18f * H)).CopyWithNewOpacity(E), -1);
		// alt yazı
		const FString Su = Sub.Get().ToString();
		DrText(Out, L + 3, G, Su, FVector2D(Tx + 2.f, Ty + Ts.Y * 0.5f + 14.f), Font(13, "Regular"), FLinearColor(0.86f, 0.9f, 0.96f, (0.55f + 0.35f * H) * E), -1);
		// vurgu çizgisi: başlığın altında, üzerine gelince başlık boyunca uzar
		const float Lw = FMath::Lerp(bPrimary ? 56.f : 26.f, (float)Ts.X, H);
		DrBox(Out, L + 4, G, FVector2D(Tx, Ty + Ts.Y * 0.5f + 28.f), FVector2D(Lw * E, 2.f), Ac.CopyWithNewOpacity((bPrimary ? 0.95f : 0.5f + 0.5f * H) * E), Round(1.f));
		if (bPrimary)
		{
			// UÇUŞ: nefes alan ışıltı + sağda ok
			const float P = 0.6f + 0.4f * FMath::Sin((float)Now() * 2.2f);
			DrGlow(Out, L, G, FVector2D(Tx + Ts.X * 0.5f, Ty), (float)Ts.X * 0.55f, Ac.CopyWithNewOpacity(0.10f * P * E));
			DrIcon(Out, L + 4, G, ESmvIcon::Next, FVector2D(Tx + Ts.X + 34.f + H * 10.f, Ty), 22.f, Ac.CopyWithNewOpacity(E), 2.5f);
		}
		if (bFocus && PadActive())
			DrBox(Out, L + 5, G, FVector2D(4, 6), FVector2D(3, S.Y - 12), Ac, Round(1.5f));
		return L + 6;
	}

private:
	FText Title;
	TAttribute<FText> Sub;
	int32 Index = 1;
	bool bPrimary = false;
	float Delay = 0.f;
	FLinearColor AccentC;
	TFunction<void()> OnClick;
	double Born = 0;
	bool bHover = false, bFocus = false, bPressed = false;
	float HoverA = 0.f, PressA = 0.f;
	void Click() { FSmvUiSound::Play(ESmvSnd::Click); if (OnClick) OnClick(); }
};

// ---------------------------------------------------------------------------
// ortak parçalar
// ---------------------------------------------------------------------------

bool SSmvShell::IsSceneScreen(ESmvScreen S)
{
	switch (S)
	{
	case ESmvScreen::Menu: case ESmvScreen::Welcome: case ESmvScreen::Hangar: case ESmvScreen::HangarWizard: case ESmvScreen::Crew: case ESmvScreen::CrewWizard:
	case ESmvScreen::Customize: case ESmvScreen::Tracks: case ESmvScreen::ParkurList: case ESmvScreen::ParkurWizard: case ESmvScreen::RealWizard:
	case ESmvScreen::FlyFlow: case ESmvScreen::FlyMode: case ESmvScreen::Progress: case ESmvScreen::Friends:
		return true;
	default:
		return false;
	}
}

void SSmvShell::UpdateStation()
{
	ASmvMenuScene* S = Scene();
	if (!S || !S->IsLive() || !Flow.IsValid()) return;
	using EStation = ASmvMenuScene::EStation;
	EStation St = EStation::Menu;
	switch (Built)
	{
	case ESmvScreen::Hangar: case ESmvScreen::HangarWizard: St = EStation::Hangar; break;
	case ESmvScreen::Crew: case ESmvScreen::CrewWizard: case ESmvScreen::Customize: St = EStation::Crew; break;
	case ESmvScreen::Tracks: case ESmvScreen::ParkurWizard: case ESmvScreen::RealWizard: St = EStation::Parkur; break;
	case ESmvScreen::ParkurList: St = TourKey.IsEmpty() ? EStation::Parkur : EStation::Tour; break;
	case ESmvScreen::FlyFlow: case ESmvScreen::FlyMode: St = EStation::Fly; break;
	default: St = EStation::Menu; break;
	}
	S->SetStation(St);
	// vitrin: hangarda düzenlenen dron, uçuşta seçilen dron; mürettebatta seçili / yeni pilot
	if (St == EStation::Hangar) S->SetShowcase(EditDrone);
	else if (St == EStation::Fly) S->SetShowcase(Flow->Draft.Drone);
	if (St == EStation::Crew)
	{
		const FSmvAvatar& A = Built == ESmvScreen::CrewWizard ? NewPilot.Avatar : Built == ESmvScreen::Customize ? EditPilot.Avatar
			: (Data() && Data()->DefaultPilot() ? Data()->DefaultPilot()->Avatar : EditPilot.Avatar);
		S->SetShowcasePilot(A, (Built == ESmvScreen::CrewWizard && CwStep == 2) || (Built == ESmvScreen::Customize && bFaceZoom));
	}
	// kamera türü önizlemesi yalnız sihirbazın kamera adımında
	S->SetCameraFx(Built == ESmvScreen::HangarWizard && HwStep == 3 ? EditDrone.Equipment.Camera : FString(TEXT("day")));
	// karşılaşma kartı (Parkur merkezi) canlı ikinci görünüm
	S->SetCardActive(Built == ESmvScreen::Tracks);
	// karşılaşma parkuru turu: menü haritası seçilen Fab haritasına geçer; turdan çıkınca menü haritasına döner
	static FString MenuDefaultMap;
	if (MenuDefaultMap.IsEmpty()) MenuDefaultMap = S->GetMapKey().IsEmpty() ? FString(TEXT("proc")) : S->GetMapKey();
	if (Built == ESmvScreen::ParkurList && !TourKey.IsEmpty()) S->SetMap(TourKey);
	else if (Built != ESmvScreen::ParkurList && S->GetMapKey() != MenuDefaultMap && !(MenuDefaultMap == TEXT("proc") && S->GetMapKey().IsEmpty())) S->SetMap(MenuDefaultMap);
}

TSharedRef<SWidget> SSmvShell::YhPage(const FText& InTitle, const FText& Sub, TSharedRef<SWidget> Body, TFunction<void()> OnBack, TSharedPtr<SWidget> TitleExtra)
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	TFunction<void()> Back = OnBack ? OnBack : TFunction<void()>([F]() { if (F.IsValid()) F->Back(); });
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(48, 26, 48, 10)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 22, 0)
			[
				SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Back).Text(Tx(TEXT("common.back"))).OnClick(Back)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Title(InTitle, 30)]
				+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 0, 0)[SmvUi::Text(Sub, 12, "Regular", Dim(), false)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(22, 0, 0, 0)
			[
				TitleExtra.IsValid() ? TitleExtra.ToSharedRef() : SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[PilotChip()]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(48, 4, 48, 0)
		[
			Body
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(48, 8, 48, 18)
		[
			HintFooter(true)
		];
}

TSharedRef<SWidget> SSmvShell::YhSteps(const TArray<FText>& Steps, int32 Cur, TFunction<void(int32)> OnPick)
{
	TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
	for (int32 i = 0; i < Steps.Num(); ++i)
	{
		const bool bDone = i < Cur, bNow = i == Cur;
		H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
		[
			SNew(SSmvButton).Kind(ESmvBtn::Ghost).FontSize(12).Selected(bNow).Accent(bDone ? Accent3() : Accent())
			.Icon(bDone ? ESmvIcon::Check : ESmvIcon::None)
			.Text(FText::FromString(FString::Printf(TEXT("%d  %s"), i + 1, *Steps[i].ToString())))
			.IsEnabled(i <= Cur + 1)
			.OnClick([OnPick, i]() { if (OnPick) OnPick(i); })
		];
		if (i < Steps.Num() - 1)
			H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SBox).WidthOverride(16.f).HeightOverride(2.f)[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(bDone ? Accent3() : Edge())]];
	}
	return H;
}

TSharedRef<SWidget> SSmvShell::XpBar(int64 Xp, float W, float H)
{
	TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(W, H));
	const float P = (float)SmvCore::LevelProgress(Xp);
	const double Born = FPlatformTime::Seconds();
	C->Painter = [P, Born](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D S = FVector2D(G.GetLocalSize());
		const float K = ReducedMotion() ? 1.f : FMath::Clamp((float)(FPlatformTime::Seconds() - Born) * 2.f, 0.f, 1.f);
		DrBox(Out, L, G, FVector2D::ZeroVector, S, FLinearColor(1, 1, 1, 0.12f), Round(S.Y * 0.5f));
		DrGradient(Out, L + 1, G, FVector2D::ZeroVector, FVector2D(S.X * P * (1.f - FMath::Pow(1.f - K, 3.f)), S.Y), { { 0.f, Accent() }, { 1.f, Accent3() } }, false);
	};
	return C;
}

TSharedRef<SWidget> SSmvShell::PilotChip()
{
	const FSmvPilot* P = Data() ? Data()->DefaultPilot() : nullptr;
	const int64 Xp = P ? P->Xp : 0;
	const int32 Lv = SmvCore::LevelForXp(Xp);
	const FString Name = P ? P->Name : FString(TEXT("Pilot"));
	const bool bIn = Client() && Client()->IsLoggedIn();
	SSmvShell* Self = this;
	TSharedRef<SWidget> Face = Portrait(P ? &P->Avatar : nullptr, 38.f);
	if (Scene() && P)
	{
		if (UTextureRenderTarget2D* Rt = Scene()->AvatarSnap(P->Avatar))
			if (const FSlateBrush* B = YhRtBrush(YhBrushes, Rt, FVector2D(42, 42)))
				Face = SNew(SBox).WidthOverride(42.f).HeightOverride(42.f)[SNew(SImage).Image(B)];
	}
	return SNew(SSmvButton).Kind(ESmvBtn::Ghost).Accent(Accent()).OnClick([Self]() { Self->OpenPilotMenu(); })
		.Extra()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(Hex(0x0b1222, 0.85f)).Padding(2.f)[Face]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(Name), 13, "Bold", Fg(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
					[
						SNew(SBorder).BorderImage(Round(8.f)).BorderBackgroundColor(Accent().CopyWithNewOpacity(0.25f)).Padding(FMargin(7.f, 1.f))
						[
							SmvUi::Text(FSmvI18n::Format(TEXT("level.short"), { { TEXT("n"), FString::FromInt(Lv) } }), 10, "Bold", Accent(), false)
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 1)[XpBar(Xp, 150.f, 4.f)]
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(bIn ? FText::FromString(TEXT("simumav.com")) : Tx(TEXT("account.guest")), 9, "Regular", Mute(), false)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)[Icon(ESmvIcon::Next, 14.f, Dim())]
		];
}

TSharedRef<SWidget> SSmvShell::DroneThumb(const FSmvDrone& D, float W, float H)
{
	return ModelThumb(D.Type, D.ModelKey, W, H);
}

TSharedRef<SWidget> SSmvShell::ModelThumb(const FString& Type, const FString& Model, float W, float H)
{
	UTextureRenderTarget2D* Rt = (Scene() && FSmvUiPrefs::Get().bPreview3d) ? Scene()->DroneSnap(Type, Model, TEXT("#cfd8dc")) : nullptr;
	const FSlateBrush* B = YhRtBrush(YhBrushes, Rt, FVector2D(240, 150));
	return SNew(SBox).WidthOverride(W).HeightOverride(H)
		[
			B ? YhW<SImage>(SNew(SImage).Image(B)) : YhW<SBox>(SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[Icon(IconForType(Type), H * 0.5f, Accent())])
		];
}

TSharedRef<SWidget> SSmvShell::SatThumb(double Lat, double Lon, float W, float H, bool bAnimate)
{
	TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(W, H)).Visibility(EVisibility::HitTestInvisible);
	const double Born = FPlatformTime::Seconds();
	C->Painter = [Lat, Lon, bAnimate, Born](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D S = FVector2D(G.GetLocalSize());
		const double T = bAnimate ? FPlatformTime::Seconds() - Born : 0.0;
		// yavaş kaydırma + nefes alan yakınlaşma (uydu görüntüsü "video döngüsü")
		const double Mpp = 4.0 * (1.0 + 0.25 * FMath::Sin(T * 0.12));
		const double CLat = Lat + 0.0012 * FMath::Sin(T * 0.05), CLon = Lon + 0.0016 * FMath::Cos(T * 0.04);
		const FVector2D Ctr = S * 0.5;
		const double CosL = FMath::Cos(FMath::DegreesToRadians(CLat));
		auto ToScr = [Ctr, CLat, CLon, Mpp, CosL](double La, double Lo) { return Ctr + FVector2D((Lo - CLon) * 111320.0 * CosL / Mpp, -(La - CLat) * 110574.0 / Mpp); };
		DrBox(Out, L, G, FVector2D::ZeroVector, S, Hex(0x0b1222, 1.f), nullptr);
		Out.PushClip(FSlateClippingZone(G));
		SSmvTileMap::PaintImagery(Out, L + 1, G, CLat, CLon, Mpp, ToScr, 1.f);
		const FVector2D Pin = ToScr(Lat, Lon);
		const float Pulse = (float)FMath::Fmod(FPlatformTime::Seconds() * 0.6, 1.0);
		DrArc(Out, L + 3, G, Pin, 6.f + 22.f * Pulse, 0.f, 360.f, Accent().CopyWithNewOpacity(1.f - Pulse), 2.f);
		DrBox(Out, L + 4, G, Pin - FVector2D(5, 5), FVector2D(10, 10), Warn(), Circle());
		Out.PopClip();
	};
	return C;
}

void SSmvShell::OpenChoice(const FText& InTitle, const FText& Body, const TArray<FChoice>& Items)
{
	if (!OverlayBox.IsValid()) return;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	TSharedPtr<SWidget> First;
	for (const FChoice& C : Items)
	{
		const TFunction<void()> Fn = C.Fn;
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(C.Kind == 2 ? ESmvBtn::Danger : C.Kind == 1 ? ESmvBtn::Primary : ESmvBtn::Ghost).Icon(C.Icon).MinWidth(360.f).Text(C.Label)
			.OnClick([this, Fn]() { CloseOverlay(); if (Fn) Fn(); });
		if (!First.IsValid()) First = B;
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[B];
	}
	const double At = FPlatformTime::Seconds();
	OverlayBox->SetContent(
		SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(0.f)
		.BorderBackgroundColor_Lambda([At]() { return FSlateColor(FLinearColor(0.f, 0.f, 0.02f, 0.55f * FMath::Clamp((float)(FPlatformTime::Seconds() - At) * 6.f, 0.f, 1.f))); })
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseOverlay(); return FReply::Handled(); })
		[
			SNew(SBox).WidthOverride(440.f)
			[
				SNew(SBorder).BorderImage(GlassCard()).Padding(FMargin(26.f, 22.f))
				.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Title(InTitle, 20)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 18)[SmvUi::Text(Body, 12, "Regular", Dim())]
					+ SVerticalBox::Slot().AutoHeight()[List]
					+ SVerticalBox::Slot().AutoHeight()[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Back).MinWidth(360.f).Text(Tx(TEXT("common.cancel"))).OnClick([this]() { CloseOverlay(); })]
				]
			]
		]);
	bOverlay = true;
	FSmvUiSound::Play(ESmvSnd::Open);
	DefaultFocus = First;
	bWantFocus = true;
}

void SSmvShell::OpenPilotMenu()
{
	if (!OverlayBox.IsValid() || !Flow.IsValid()) return;
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	const FSmvPilot* P = Data() ? Data()->DefaultPilot() : nullptr;
	const int64 Xp = P ? P->Xp : 0;
	const int32 Lv = SmvCore::LevelForXp(Xp);
	const long long Next = SmvCore::XpForLevel(FMath::Min(Lv + 1, 100));
	const bool bIn = Client() && Client()->IsLoggedIn();
	TSharedRef<SVerticalBox> Items = SNew(SVerticalBox);
	TSharedPtr<SWidget> First;
	auto Item = [this, &Items, &First](const FText& L, ESmvIcon I, TFunction<void()> Fn, bool bDanger = false)
	{
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(bDanger ? ESmvBtn::Ghost : ESmvBtn::Ghost).LeftAlign(true).MinWidth(300.f).Icon(I).FontSize(13).Text(L)
			.Accent(bDanger ? Danger() : Accent())
			.OnClick([this, Fn]() { CloseOverlay(); if (Fn) Fn(); });
		if (!First.IsValid()) First = B;
		Items->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[B];
	};
	auto Sep = [&Items]() { Items->AddSlot().AutoHeight().Padding(0, 6, 0, 8)[SNew(SBox).HeightOverride(1.f)[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Edge())]]; };
	auto Go = [F](ESmvScreen S) { return [F, S]() { if (F.IsValid()) F->SetScreen(S); }; };
	Item(Tx(TEXT("pilotmenu.customize")), ESmvIcon::Pilot, Go(ESmvScreen::Customize));
	Item(Tx(TEXT("pilotmenu.flights")), ESmvIcon::Replay, Go(ESmvScreen::Logbook));
	Item(Tx(TEXT("pilotmenu.friends")), ESmvIcon::User, Go(ESmvScreen::Friends));
	Sep();
	Item(Tx(TEXT("menu.top10")), ESmvIcon::Trophy, Go(ESmvScreen::Leaderboard));
	Item(Tx(TEXT("menu.settings")), ESmvIcon::Gear, Go(ESmvScreen::Settings));
	Item(bIn ? Tx(TEXT("menu.account")) : Tx(TEXT("welcome.login")), ESmvIcon::Globe, Go(bIn ? ESmvScreen::Account : ESmvScreen::Login));
	Sep();
	Item(Tx(TEXT("menu.missions")), ESmvIcon::Mission, Go(ESmvScreen::Missions));
	Item(Tx(TEXT("pilotmenu.fpv")), ESmvIcon::Fpv, Go(ESmvScreen::FpvGallery));
	Item(Tx(TEXT("menu.match")), ESmvIcon::Match, Go(ESmvScreen::Lobby));
	Item(Tx(TEXT("pilotmenu.replay")), ESmvIcon::Camera, Go(ESmvScreen::Replay));
	Item(Tx(TEXT("support.help")), ESmvIcon::Info, Go(ESmvScreen::Support));
	Item(Tx(TEXT("menu.help")), ESmvIcon::Help, [Self]() { Self->ToggleHelp(); });
	Sep();
	Item(Tx(TEXT("menu.quit")), ESmvIcon::Power, [Self, G = GameMode]()
	{
		Self->Confirm(FSmvI18n::T(TEXT("dlg.quit_title")), FSmvI18n::T(TEXT("dlg.quit_body")), FSmvI18n::T(TEXT("dlg.yes_quit")), true,
			[G]() { if (G.IsValid()) UKismetSystemLibrary::QuitGame(G.Get(), nullptr, EQuitPreference::Quit, false); });
	}, true);
	// madalyalar (kazanılan renkli)
	TSharedRef<SHorizontalBox> Medals = SNew(SHorizontalBox);
	for (const SmvCore::FMedal& M : SmvCore::Medals())
	{
		const bool bHas = Lv >= M.Level;
		Medals->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Icon(ESmvIcon::Star, 18.f, bHas ? YhMedalColor(M.Level) : FLinearColor(1, 1, 1, 0.15f))];
	}
	const double At = FPlatformTime::Seconds();
	OverlayBox->SetContent(
		SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(0.f)
		.BorderBackgroundColor_Lambda([At]() { return FSlateColor(FLinearColor(0.f, 0.f, 0.02f, 0.35f * FMath::Clamp((float)(FPlatformTime::Seconds() - At) * 6.f, 0.f, 1.f))); })
		.HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0.f, 84.f, 44.f, 0.f))
		.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { CloseOverlay(); return FReply::Handled(); })
		[
			SNew(SBox).WidthOverride(340.f)
			.RenderTransform_Lambda([At]()
			{
				const float K = ReducedMotion() ? 1.f : FMath::Clamp((float)(FPlatformTime::Seconds() - At) * 6.f, 0.f, 1.f);
				return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(0.f, (1.f - K) * -14.f)));
			})
			[
				SNew(SBorder).BorderImage(GlassCard()).Padding(FMargin(18.f, 16.f))
				.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)[Portrait(P ? &P->Avatar : nullptr, 54.f)]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(P ? P->Name : FString(TEXT("Pilot"))), 16, "Bold", Fg(), false)]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)[SmvUi::Text(FSmvI18n::Format(TEXT("level.line"), { { TEXT("n"), FString::FromInt(Lv) },
								{ TEXT("xp"), FString::Printf(TEXT("%lld"), (long long)Xp) }, { TEXT("next"), FString::Printf(TEXT("%lld"), Next) } }), 11, "Regular", Dim(), false)]
							+ SVerticalBox::Slot().AutoHeight()[XpBar(Xp, 220.f, 5.f)]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 6)[Medals]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Items]
				]
			]
		]);
	bOverlay = true;
	FSmvUiSound::Play(ESmvSnd::Open);
	DefaultFocus = First;
	bWantFocus = true;
}

// ---------------------------------------------------------------------------
// ana menü (sade): Hangar · Parkur · Mürettebat · Uçuş + Pilot menüsü
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::MenuV2()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	auto Go = [F](ESmvScreen S) { return [F, S]() { if (F.IsValid()) F->SetScreen(S); }; };
	const USmvDataService* D = Data();
	const FSmvDrone* Dr = D ? D->DefaultDrone() : nullptr;
	const FSmvPilot* Pl = D ? D->DefaultPilot() : nullptr;
	const int32 NDr = D ? D->Drones().Num() : 0;
	int32 NCustom = 0, NReal = 0;
	if (D) for (const FSmvTrack& T : D->Tracks()) { if (T.IsReal()) ++NReal; else if (T.IsCustom()) ++NCustom; }
	const FText HangarSub = FSmvI18n::Format(TEXT("menu2.hangar_sub"), { { TEXT("n"), FString::FromInt(NDr) }, { TEXT("d"), Dr ? Dr->Name : FString(TEXT("—")) } });
	const FText ParkurSub = FSmvI18n::Format(TEXT("menu2.parkur_sub"), { { TEXT("e"), FString::FromInt(FSmvFabContent::Maps().Num()) }, { TEXT("c"), FString::FromInt(NCustom) }, { TEXT("r"), FString::FromInt(NReal) } });
	const FText CrewSub = FSmvI18n::Format(TEXT("menu2.crew_sub"), { { TEXT("p"), Pl ? Pl->Name : FString(TEXT("Pilot")) }, { TEXT("n"), FString::FromInt(SmvCore::LevelForXp(Pl ? Pl->Xp : 0)) } });
	TSharedRef<SSmvMenuEntry> Fly = SNew(SSmvMenuEntry).Index(4).Primary(true).Delay(0.36f).Accent(Hex(0xfbbf24)).Title(Tx(TEXT("menu2.fly"))).Sub(Tx(TEXT("menu2.fly_sub")))
		.OnClick([F]() { if (F.IsValid()) { F->FlyStep = 0; F->SetScreen(ESmvScreen::FlyFlow); } });
	TSharedRef<SSmvMenuEntry> Hg = SNew(SSmvMenuEntry).Index(1).Delay(0.05f).Title(Tx(TEXT("menu.hangar"))).Sub(HangarSub).OnClick(Go(ESmvScreen::Hangar));
	DefaultFocus = Hg;
	const bool bSitl = USmvLocalFlightLink::SitlAvailable();
	const bool bIn = Client() && Client()->IsLoggedIn();
	const FString MapName = Scene() && Scene()->IsLive() && !Scene()->GetMapName().IsEmpty() ? Scene()->GetMapName().ToString() : FString();
	TSharedRef<SHorizontalBox> Status = SNew(SHorizontalBox);
	auto Dot = [&Status](bool bOk, const FText& T)
	{
		Status->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SBox).WidthOverride(7.f).HeightOverride(7.f)[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(bOk ? Accent3() : Warn())]];
		Status->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 22, 0)[SmvUi::Text(T, 11, "Regular", FLinearColor(1, 1, 1, 0.72f), false)];
	};
	Dot(bSitl, bSitl ? Tx(TEXT("menu2.sitl_ok")) : Tx(TEXT("menu2.sitl_missing")));
	Dot(bIn, bIn ? FText::FromString(TEXT("simumav.com")) : Tx(TEXT("account.guest")));
	if (!MapName.IsEmpty()) Dot(true, FSmvI18n::Format(TEXT("menu2.scene"), { { TEXT("map"), MapName } }));
	const FString Season = SeasonId();
	return SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(64, 40, 0, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("SIMU"))).Font(Font(46, "Black", 160)).ColorAndOpacity(Fg()).ShadowOffset(FVector2D(0, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f))]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("MAV"))).Font(Font(46, "Black", 160)).ColorAndOpacity(Accent()).ShadowOffset(FVector2D(0, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f))]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(3, 0, 0, 0)[SmvUi::Text(Tx(TEXT("welcome.tagline")), 13, "Light", FLinearColor(1, 1, 1, 0.75f), false)]
			+ SVerticalBox::Slot().AutoHeight().Padding(3, 8, 0, 0)
			[
				Season.IsEmpty() ? SNullWidget::NullWidget : SmvUi::Text(FSmvI18n::T(TEXT("holiday.greet_") + Season), 12, "Bold", SeasonColor(Season), false)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 34, 44, 0)
		[
			PilotChip()
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(40, 70, 0, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[Hg]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[SNew(SSmvMenuEntry).Index(2).Delay(0.14f).Title(Tx(TEXT("menu2.parkur"))).Sub(ParkurSub).OnClick(Go(ESmvScreen::Tracks))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[SNew(SSmvMenuEntry).Index(3).Delay(0.24f).Title(Tx(TEXT("menu.crew"))).Sub(CrewSub).OnClick(Go(ESmvScreen::Crew))]
			+ SVerticalBox::Slot().AutoHeight()[Fly]
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(64, 0, 44, 22)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Status]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 18, 0)[HintFooter(false)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(TEXT("Simumav · UE 5.8 · ArduPilot SITL")), 10, "Regular", FLinearColor(1, 1, 1, 0.4f), false)]
		];
}

// ---------------------------------------------------------------------------
// HANGAR: kayıtlı dronlar (gerçek sahnede vitrin) + "+ Yeni" sihirbazı; dron tıklanınca Düzenle / Sil
// ---------------------------------------------------------------------------

namespace
{
	void YhTypeStats(const FString& T, float Out[4])
	{
		const float S[7][4] = { { 45, 40, 70, 65 }, { 70, 90, 60, 35 }, { 55, 50, 55, 60 }, { 75, 85, 65, 45 }, { 85, 25, 35, 100 }, { 100, 60, 40, 80 }, { 30, 45, 90, 50 } };
		const int32 I = T == TEXT("plane") ? 1 : T == TEXT("heli") ? 2 : T == TEXT("vtol") ? 3 : T == TEXT("fpv") ? 4 : T == TEXT("interceptor") ? 5 : T == TEXT("rover") ? 6 : 0;
		for (int32 k = 0; k < 4; ++k) Out[k] = S[I][k] / 100.f;
	}

	/** Tipe göre ArduPilot parametreleri (sihirbaz "Parametreler" adımı): ad, dil anahtarı, alt / üst sınır, adım, varsayılan */
	struct FYhParam { const TCHAR* Name; const TCHAR* Key; double Min, Max, Step, Def; };
	TArray<FYhParam> YhParamsFor(const FString& Type)
	{
		TArray<FYhParam> P;
		if (Type == TEXT("multirotor") || Type == TEXT("fpv") || Type == TEXT("heli"))
		{
			P.Add({ TEXT("PILOT_SPEED_UP"), TEXT("prm.pilot_speed_up"), 50, 1000, 10, 250 });
			P.Add({ TEXT("PILOT_SPEED_DN"), TEXT("prm.pilot_speed_dn"), 30, 500, 10, 150 });
			P.Add({ TEXT("ANGLE_MAX"), TEXT("prm.angle_max"), 1000, 8000, 100, 3000 });
			P.Add({ TEXT("WPNAV_ACCEL"), TEXT("prm.wpnav_accel"), 50, 500, 10, 250 });
			P.Add({ TEXT("RTL_ALT"), TEXT("prm.rtl_alt_cm"), 300, 30000, 100, 1500 });
			if (Type == TEXT("fpv")) P.Add({ TEXT("ACRO_RP_RATE"), TEXT("prm.acro_rate"), 90, 1080, 10, 360 });
		}
		else if (Type == TEXT("rover"))
		{
			P.Add({ TEXT("CRUISE_THROTTLE"), TEXT("prm.cruise_thr"), 10, 100, 1, 50 });
			P.Add({ TEXT("ATC_STR_RAT_MAX"), TEXT("prm.steer_rate"), 30, 360, 5, 120 });
			P.Add({ TEXT("WP_RADIUS"), TEXT("prm.wp_radius"), 0.5, 10, 0.5, 2 });
		}
		else
		{
			P.Add({ TEXT("AIRSPEED_CRUISE"), TEXT("prm.airspeed_cruise"), 8, 60, 1, 22 });
			P.Add({ TEXT("AIRSPEED_MIN"), TEXT("prm.airspeed_min"), 6, 40, 1, 12 });
			P.Add({ TEXT("ROLL_LIMIT_DEG"), TEXT("prm.roll_limit"), 20, 80, 1, 45 });
			P.Add({ TEXT("PTCH_LIM_MAX_DEG"), TEXT("prm.pitch_max"), 10, 45, 1, 20 });
			P.Add({ TEXT("PTCH_LIM_MIN_DEG"), TEXT("prm.pitch_min"), -45, -5, 1, -25 });
			P.Add({ TEXT("RTL_ALTITUDE"), TEXT("prm.rtl_alt_m"), 20, 500, 5, 100 });
			P.Add({ TEXT("TECS_CLMB_MAX"), TEXT("prm.climb_max"), 1, 15, 0.5, 5 });
			if (Type == TEXT("vtol") || Type == TEXT("interceptor"))
			{
				P.Add({ TEXT("Q_ANGLE_MAX"), TEXT("prm.q_angle_max"), 1000, 8000, 100, 3000 });
				P.Add({ TEXT("Q_ASSIST_SPEED"), TEXT("prm.q_assist"), 0, 30, 1, 0 });
			}
		}
		return P;
	}

	/** Kayıtlı parametrelerden azami hız (m/s; yoksa −1) */
	double YhMaxSpeedOf(const FSmvDrone& D)
	{
		if (D.Type == TEXT("rover")) { if (const double* V = D.Params.Find(TEXT("WP_SPEED"))) return *V; return -1; }
		if (D.Type == TEXT("plane") || D.Type == TEXT("vtol") || D.Type == TEXT("interceptor"))
		{
			if (D.Power.MaxSpeedMs > 0) return D.Power.MaxSpeedMs;
			if (const double* V = D.Params.Find(TEXT("AIRSPEED_MAX"))) return *V;
			return -1;
		}
		if (const double* V = D.Params.Find(TEXT("WPNAV_SPEED"))) return *V / 100.0;
		return -1;
	}

	FString YhDefaultTypeFor(const FString& Kind)
	{
		if (Kind == TEXT("plane")) return TEXT("plane");
		if (Kind == TEXT("vtol")) return TEXT("vtol");
		if (Kind == TEXT("rover")) return TEXT("rover");
		return TEXT("multirotor");
	}
}

TSharedRef<SWidget> SSmvShell::HangarV2()
{
	USmvDataService* D = Data();
	if (!D || !Flow.IsValid()) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	if (EditDrone.Id.IsEmpty() && D->Drones().Num()) EditDrone = D->DefaultDrone() ? *D->DefaultDrone() : D->Drones()[0];
	// sol: kayıtlı dronlar
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FSmvDrone& X : D->Drones())
	{
		const FSmvDrone Copy = X;
		const FSmvCatalogModel* M = USmvDataService::FindModel(X.ModelKey);
		TSharedRef<SHorizontalBox> Badges = SNew(SHorizontalBox);
		auto Badge = [&Badges](ESmvIcon I, const FLinearColor& C) { Badges->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Icon(I, 14.f, C)]; };
		const FSmvEquipment& Eq = X.Equipment;
		Badge(Eq.Camera == TEXT("night") ? ESmvIcon::Moon : Eq.Camera == TEXT("thermal") ? ESmvIcon::Bolt : Eq.Camera == TEXT("multispectral") ? ESmvIcon::Sun : ESmvIcon::Camera, Dim());
		if (Eq.bSprayer) Badge(ESmvIcon::Rain, Accent3());
		if (Eq.bGripper) Badge(ESmvIcon::Next, Warn());
		if (Eq.bParachute) Badge(ESmvIcon::Ghost, Danger());
		if (!X.Script.IsEmpty()) Badge(ESmvIcon::Book, Accent2());
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Normal).LeftAlign(true).MinWidth(340.f).Accent(YhHex(X.Color, Accent())).Selected(X.Id == EditDrone.Id)
			.OnClick([Self, F, Copy]()
			{
				Self->EditDrone = Copy;
				Self->Refresh();
				// yol haritası: kayıtlı dron tıklanınca sor — Düzenle / Sil
				TArray<FChoice> Ch;
				FChoice E1; E1.Label = YhT(TEXT("hangar2.edit")); E1.Icon = ESmvIcon::Gear; E1.Kind = 1;
				E1.Fn = [Self, F, Copy]() { Self->EditDrone = Copy; Self->bHwEdit = true; Self->HwStep = 2; Self->HwKind = YhKindOf(Copy.Type); Self->HwMaxSpeed = YhMaxSpeedOf(Copy); if (F.IsValid()) F->SetScreen(ESmvScreen::HangarWizard); };
				Ch.Add(E1);
				FChoice E2; E2.Label = YhT(TEXT("common.make_default")); E2.Icon = ESmvIcon::Star;
				E2.Fn = [Self, Copy]() { if (Self->Data()) Self->Data()->SetDefault(TEXT("drone"), Copy.Id); Self->Refresh(); };
				Ch.Add(E2);
				FChoice E3; E3.Label = YhT(TEXT("common.delete")); E3.Icon = ESmvIcon::Trash; E3.Kind = 2;
				E3.Fn = [Self, Copy]()
				{
					Self->Confirm(FSmvI18n::T(TEXT("dlg.delete_title")), FSmvI18n::Format(TEXT("dlg.delete_body"), { { TEXT("n"), Copy.Name } }), FSmvI18n::T(TEXT("dlg.yes_delete")), true, [Self, Copy]()
					{
						Self->Data()->DeleteDrone(Copy.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& Msg)
						{
							Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : Msg, bOk ? 6 : 3);
							if (bOk && Self->Data()->Drones().Num()) Self->EditDrone = Self->Data()->Drones()[0];
							Self->Refresh();
						}));
					});
				};
				Ch.Add(E3);
				Self->OpenChoice(FText::FromString(Copy.Name), YhT(TEXT("hangar2.ask")), Ch);
			})
			.Extra()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[DroneThumb(X, 112.f, 70.f)]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString((X.bIsDefault ? FString(TEXT("★ ")) : FString()) + X.Name), 14, "Bold", Fg(), false)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 5)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *FSmvI18n::T(FString(TEXT("type.")) + X.Type).ToString(), M ? *M->Label : *X.ModelKey)), 10, "Regular", Dim(), false)]
					+ SVerticalBox::Slot().AutoHeight()[Badges]
				]
			];
		if (X.Id == EditDrone.Id) DefaultFocus = B;
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[B];
	}
	// sağ: seçili dronun bilgi kartı
	const FSmvDrone& Ed = EditDrone;
	const FSmvCatalogModel* Cm = USmvDataService::FindModel(Ed.ModelKey);
	float St[4];
	YhTypeStats(Ed.Type, St);
	TSharedRef<SVerticalBox> Info = SNew(SVerticalBox);
	Info->AddSlot().AutoHeight()[Title(FText::FromString(Ed.Name.IsEmpty() ? FString(TEXT("—")) : Ed.Name), 24)];
	Info->AddSlot().AutoHeight().Padding(0, 2, 0, 12)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *FSmvI18n::T(FString(TEXT("type.")) + Ed.Type).ToString(), Cm ? *Cm->Label : *Ed.ModelKey)), 12, "Regular", Dim(), false)];
	Info->AddSlot().AutoHeight()[StatBar(Tx(TEXT("stat.speed")), St[0], Accent())];
	Info->AddSlot().AutoHeight()[StatBar(Tx(TEXT("stat.range")), St[1], Accent3())];
	Info->AddSlot().AutoHeight()[StatBar(Tx(TEXT("stat.durability")), St[2], Warn())];
	Info->AddSlot().AutoHeight()[StatBar(Tx(TEXT("stat.agility")), St[3], Accent2())];
	auto Line = [&Info](ESmvIcon I, const FText& K, const FString& V)
	{
		Info->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Icon(I, 16.f, Accent())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SBox).WidthOverride(120.f)[SmvUi::Text(K, 11, "Regular", Dim(), false)]]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(V), 12, "Bold", Fg(), false)]
		];
	};
	Info->AddSlot().AutoHeight().Padding(0, 12, 0, 2)[Section(Tx(TEXT("hangar2.equipment")), Accent())];
	Line(ESmvIcon::Camera, Tx(TEXT("hw.camera")), FSmvI18n::T(FString(TEXT("cam2.")) + Ed.Equipment.Camera).ToString());
	{
		TArray<FString> Ad;
		if (Ed.Equipment.bSprayer) Ad.Add(FSmvI18n::T(TEXT("addon.sprayer")).ToString());
		if (Ed.Equipment.bGripper) Ad.Add(FSmvI18n::T(TEXT("addon.gripper")).ToString());
		if (Ed.Equipment.bParachute) Ad.Add(FSmvI18n::T(TEXT("addon.parachute")).ToString());
		Line(ESmvIcon::Plus, Tx(TEXT("hw.addons")), Ad.Num() ? FString::Join(Ad, TEXT(", ")) : FSmvI18n::T(TEXT("hangar2.none")).ToString());
	}
	{
		const double Ms = YhMaxSpeedOf(Ed);
		Line(ESmvIcon::Bolt, Tx(TEXT("hw.max_speed")), Ms > 0 ? FString::Printf(TEXT("%.0f m/s"), Ms) : FSmvI18n::T(TEXT("power.default")).ToString());
		Line(ESmvIcon::Rocket, Tx(TEXT("hw.max_alt")), Ed.Equipment.MaxAltM > 0 ? FString::Printf(TEXT("%.0f m"), Ed.Equipment.MaxAltM) : FSmvI18n::T(TEXT("power.default")).ToString());
	}
	Line(ESmvIcon::Gear, Tx(TEXT("hw.params")), FSmvI18n::Format(TEXT("hangar2.params_n"), { { TEXT("n"), FString::FromInt(Ed.Params.Num()) } }).ToString());
	Line(ESmvIcon::Book, Tx(TEXT("hw.script")), Ed.Script.IsEmpty() ? FSmvI18n::T(TEXT("hangar2.none")).ToString() : FSmvI18n::Format(TEXT("hangar2.script_lines"), { { TEXT("n"), FString::FromInt(Ed.Script.Len() > 0 ? 1 + Ed.Script.Replace(TEXT("\r"), TEXT("")).Len() - Ed.Script.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT("")).Len() : 0) } }).ToString());
	Info->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[SmvUi::Text(Tx(TEXT("hangar2.mp_hint")), 10, "Regular", Mute())];
	Info->AddSlot().AutoHeight().Padding(0, 14, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Gear).Text(Tx(TEXT("hangar2.edit")))
			.IsEnabled(!Ed.Id.IsEmpty())
			.OnClick([Self, F]() { Self->bHwEdit = true; Self->HwStep = 2; Self->HwKind = YhKindOf(Self->EditDrone.Type); Self->HwMaxSpeed = YhMaxSpeedOf(Self->EditDrone); if (F.IsValid()) F->SetScreen(ESmvScreen::HangarWizard); })]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Normal).Icon(ESmvIcon::Play).Text(Tx(TEXT("hangar2.fly_this")))
			.IsEnabled(!Ed.Id.IsEmpty())
			.OnClick([Self, F]() { if (!F.IsValid()) return; F->Draft.Drone = Self->EditDrone; F->FlyStep = 1; F->SetScreen(ESmvScreen::FlyFlow); })]
	];
	TSharedRef<SWidget> NewBtn = SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Plus).Text(Tx(TEXT("hangar2.new")))
		.OnClick([Self, F]()
		{
			FSmvDrone N;
			N.Name = FSmvI18n::T(TEXT("hangar.new_name")).ToString();
			N.Type = TEXT("multirotor");
			N.ModelKey = USmvDataService::DefaultModelFor(N.Type);
			if (const FSmvCatalogModel* M = USmvDataService::FindModel(N.ModelKey)) N.Params = M->Defaults;
			Self->EditDrone = N;
			Self->bHwEdit = false;
			Self->HwStep = 0;
			Self->HwKind = TEXT("multi");
			Self->HwMaxSpeed = -1;
			if (F.IsValid()) F->SetScreen(ESmvScreen::HangarWizard);
		});
	TSharedRef<SWidget> Body = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(380.f)
			[
				SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[List]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(0, 0, 0, 18)
		[
			SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Title(FText::FromString(Ed.Name), 34)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SmvUi::Text(FText::FromString(Cm ? Cm->Label : Ed.ModelKey), 13, "Light", FLinearColor(1, 1, 1, 0.8f), false)]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(400.f).VAlign(VAlign_Top)[Glass(Info, FMargin(22.f), 0.9f)]
		];
	return YhPage(Tx(TEXT("menu.hangar")), Tx(TEXT("hangar2.sub")), Body, nullptr, NewBtn);
}

TSharedRef<SWidget> SSmvShell::HangarWizard()
{
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	const int32 MinStep = bHwEdit ? 1 : 0;
	HwStep = FMath::Clamp(HwStep, MinStep, 5);
	TabStep = [Self, MinStep](int32 D) { Self->HwStep = FMath::Clamp(Self->HwStep + D, MinStep, 5); Self->Refresh(); };
	const TArray<FText> Steps = { Tx(TEXT("hw.s_type")), Tx(TEXT("hw.s_model")), Tx(TEXT("hw.s_params")), Tx(TEXT("hw.s_camera")), Tx(TEXT("hw.s_addons")), Tx(TEXT("hw.s_look")) };
	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	switch (HwStep)
	{
	case 0: Body = HwType(); break;
	case 1: Body = HwModel(); break;
	case 2: Body = HwParams(); break;
	case 3: Body = HwCamera(); break;
	case 4: Body = HwAddons(); break;
	default: Body = HwLook(); break;
	}
	const bool bLast = HwStep >= 5;
	TSharedRef<SSmvButton> Next = SNew(SSmvButton).Kind(bLast ? ESmvBtn::Hero : ESmvBtn::Primary).MinWidth(220.f)
		.Icon(bLast ? ESmvIcon::Save : ESmvIcon::Next).Accent(bLast ? Hex(0xfbbf24) : Accent())
		.Text(bLast ? (bHwEdit ? Tx(TEXT("common.save")) : Tx(TEXT("hw.add_to_hangar"))) : Tx(TEXT("tut.next")))
		.OnClick([Self]() { if (Self->HwStep >= 5) Self->HwSave(); else { Self->HwStep++; Self->Refresh(); } });
	if (!DefaultFocus.IsValid()) DefaultFocus = Next;
	TSharedRef<SWidget> Footer = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[SNew(SSmvButton).Icon(ESmvIcon::Prev).Text(Tx(TEXT("tut.prev"))).IsEnabled(HwStep > MinStep)
			.OnClick([Self, MinStep]() { Self->HwStep = FMath::Max(MinStep, Self->HwStep - 1); Self->Refresh(); })]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 10, 0)
		[
			(bHwEdit && !bLast) ? YhW<SSmvButton>(SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Save).Text(Tx(TEXT("common.save"))).OnClick([Self]() { Self->HwSave(); })) : SNullWidget::NullWidget
		]
		+ SHorizontalBox::Slot().AutoWidth()[Next];
	const FText Sub = bHwEdit ? FSmvI18n::Format(TEXT("hw.sub_edit"), { { TEXT("n"), EditDrone.Name } }) : Tx(TEXT("hw.sub_new"));
	return YhPage(bHwEdit ? Tx(TEXT("hw.title_edit")) : Tx(TEXT("hw.title_new")), Sub,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[YhSteps(Steps, HwStep, [Self, MinStep](int32 I) { if (I >= MinStep) { Self->HwStep = I; Self->Refresh(); } })]
		+ SVerticalBox::Slot().FillHeight(1.f)[Body]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[Footer],
		[Self, F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Hangar); });
}

TSharedRef<SWidget> SSmvShell::HwType()
{
	SSmvShell* Self = this;
	struct FK { const TCHAR* Kind; const TCHAR* Type; const TCHAR* Model; const TCHAR* Title; const TCHAR* Desc; uint32 Col; };
	static const FK Kinds[] = {
		{ TEXT("multi"), TEXT("multirotor"), TEXT("glb:x500"), TEXT("hw.kind_multi"), TEXT("hw.kind_multi_d"), 0x38bdf8 },
		{ TEXT("plane"), TEXT("plane"), TEXT("smv:caymaz"), TEXT("hw.kind_plane"), TEXT("hw.kind_plane_d"), 0xfbbf24 },
		{ TEXT("vtol"), TEXT("vtol"), TEXT("glb:standard_vtol"), TEXT("hw.kind_vtol"), TEXT("hw.kind_vtol_d"), 0xa78bfa },
		{ TEXT("rover"), TEXT("rover"), TEXT("glb:rover_4x4"), TEXT("hw.kind_rover"), TEXT("hw.kind_rover_d"), 0x34d399 },
	};
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	int32 i = 0;
	for (const FK& K : Kinds)
	{
		const FString Kind = K.Kind, Type = K.Type;
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Hex(K.Col)).MinWidth(300.f).IntroDelay(0.06f * i++).Selected(HwKind == Kind)
			.OnClick([Self, Kind, Type]()
			{
				Self->HwKind = Kind;
				if (!YhTypeInKind(Self->EditDrone.Type, Kind))
				{
					Self->EditDrone.Type = YhDefaultTypeFor(Kind);
					Self->EditDrone.ModelKey = USmvDataService::DefaultModelFor(Self->EditDrone.Type);
					Self->EditDrone.Params.Reset();
					if (const FSmvCatalogModel* M = USmvDataService::FindModel(Self->EditDrone.ModelKey))
					{
						Self->EditDrone.Params = M->Defaults; Self->EditDrone.Drive = M->Drive; if (!M->Airframe.IsEmpty()) Self->EditDrone.Airframe = M->Airframe;
					}
				}
				Self->HwStep = 1;
				Self->Refresh();
			})
			.Extra()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ModelThumb(Type, K.Model, 272.f, 170.f)]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 8, 0, 0)[Title(Tx(K.Title), 20, Hex(K.Col))]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 4, 0, 0)[SNew(SBox).WidthOverride(272.f)[SmvUi::Text(Tx(K.Desc), 11, "Regular", Dim())]]
			];
		if (HwKind == Kind) DefaultFocus = B;
		Row->AddSlot().AutoWidth().Padding(0, 0, 14, 0)[B];
	}
	return SNew(SBox).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(0, 0, 0, 10))[Row];
}

TSharedRef<SWidget> SSmvShell::HwModel()
{
	SSmvShell* Self = this;
	TArray<FString> Groups;
	if (HwKind == TEXT("multi")) Groups = { TEXT("p4"), TEXT("p6"), TEXT("p8"), TEXT("heli") };
	else if (HwKind == TEXT("vtol")) Groups = { TEXT("vtol"), TEXT("interceptor") };
	else Groups = { HwKind };
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	for (const FString& Gp : Groups)
	{
		TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).PreferredSize(1100.f).UseAllottedSize(true);
		int32 N = 0;
		for (const FSmvCatalogModel& M : USmvDataService::Catalog())
		{
			if (!YhTypeInKind(M.Type, HwKind) || YhGroupOf(M) != Gp) continue;
			const FSmvCatalogModel Mc = M;
			const bool bSel = EditDrone.ModelKey == M.Key;
			TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Accent()).MinWidth(196.f).Selected(bSel).IntroDelay(0.03f * N)
				.OnClick([Self, Mc]()
				{
					Self->EditDrone.Type = Mc.Type;
					Self->EditDrone.ModelKey = Mc.Key;
					Self->EditDrone.Params = Mc.Defaults;
					Self->EditDrone.Drive = Mc.Drive;
					if (!Mc.Airframe.IsEmpty()) Self->EditDrone.Airframe = Mc.Airframe;
					// eklentiler yeni tipe uymuyorsa düşer
					FSmvEquipment& E = Self->EditDrone.Equipment;
					if (E.bSprayer && !FSmvEquipment::AddonFits(TEXT("sprayer"), Mc.Type)) E.bSprayer = false;
					if (E.bGripper && !FSmvEquipment::AddonFits(TEXT("gripper"), Mc.Type)) E.bGripper = false;
					if (E.bParachute && !FSmvEquipment::AddonFits(TEXT("parachute"), Mc.Type)) E.bParachute = false;
					Self->Refresh();
				})
				.Extra()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ModelThumb(M.Type, M.Key, 176.f, 110.f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(2, 6, 0, 0)[SmvUi::Text(FText::FromString(M.Label), 12, "Bold", Fg(), false)]
					+ SVerticalBox::Slot().AutoHeight().Padding(2, 1, 0, 0)[SmvUi::Text(FText::FromString(M.Key.StartsWith(TEXT("fab:")) ? FSmvI18n::T(TEXT("hw.fab_body")).ToString() : FSmvI18n::T(FString(TEXT("type.")) + M.Type).ToString()), 10, "Regular", Dim(), false)]
				];
			if (bSel) DefaultFocus = B;
			Wrap->AddSlot().Padding(0, 0, 10, 10)[B];
			++N;
		}
		if (N == 0) continue;
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 8)[Section(Tx(*(FString(TEXT("hw.group_")) + Gp)), Accent())];
		V->AddSlot().AutoHeight()[Wrap];
	}
	// seçili modelin teknik verileri
	const FSmvCatalogModel* Cm = USmvDataService::FindModel(EditDrone.ModelKey);
	TSharedRef<SVerticalBox> Spec = SNew(SVerticalBox);
	if (Cm)
	{
		Spec->AddSlot().AutoHeight()[Title(FText::FromString(Cm->Label), 20)];
		Spec->AddSlot().AutoHeight().Padding(0, 6, 0, 10)[SmvUi::Text(FText::FromString(Cm->Desc), 11, "Regular", Dim())];
		auto Kv = [&Spec](const FText& K, const FString& Vv)
		{
			if (Vv.IsEmpty()) return;
			Spec->AddSlot().AutoHeight().Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(130.f)[SmvUi::Text(K, 11, "Regular", Mute(), false)]]
				+ SHorizontalBox::Slot().FillWidth(1.f)[SmvUi::Text(FText::FromString(Vv), 12, "Bold", Fg(), false)]
			];
		};
		if (Cm->MassKg > 0) Kv(Tx(TEXT("spec.mass")), FString::Printf(TEXT("%.1f kg"), Cm->MassKg));
		if (Cm->SpanM > 0) Kv(Tx(TEXT("spec.span")), FString::Printf(TEXT("%.2f m"), Cm->SpanM));
		if (Cm->MaxKmh > 0) Kv(Tx(TEXT("spec.max")), FString::Printf(TEXT("%.0f km/h"), Cm->MaxKmh));
		if (Cm->EnduranceMin > 0) Kv(Tx(TEXT("spec.endurance")), FString::Printf(TEXT("%.0f min"), Cm->EnduranceMin));
		if (Cm->PayloadKg > 0) Kv(Tx(TEXT("spec.payload")), FString::Printf(TEXT("%.1f kg"), Cm->PayloadKg));
		if (Cm->Motors > 0) Kv(Tx(TEXT("spec.motors")), FString::FromInt(Cm->Motors));
		Kv(Tx(TEXT("spec.battery")), Cm->Battery);
		Kv(Tx(TEXT("spec.physics")), Cm->Physics.IsEmpty() ? Cm->SitlModel : Cm->Physics);
	}
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(640.f)
			[
				SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[Glass(V, FMargin(18.f), 0.85f)]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(360.f)[Glass(Spec, FMargin(20.f), 0.85f)]];
}

TSharedRef<SWidget> SSmvShell::HwParams()
{
	SSmvShell* Self = this;
	FSmvDrone& D = EditDrone;
	const TArray<FYhParam> Defs = YhParamsFor(D.Type);
	// işaretçiler geçerli kalsın: önce tüm anahtarlar eklenir (bu ekran yeniden kurulana dek eklenmez)
	for (const FYhParam& P : Defs) if (!D.Params.Contains(P.Name)) D.Params.Add(P.Name, P.Def);
	const bool bPlane = D.Type == TEXT("plane") || D.Type == TEXT("vtol") || D.Type == TEXT("interceptor");
	const double SpMax = D.Type == TEXT("rover") ? 30.0 : bPlane ? 120.0 : (D.Type == TEXT("fpv") ? 45.0 : 30.0);
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("hw.perf")), Accent())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hw.max_speed")), Slider(&HwMaxSpeed, -1, SpMax, 1, [](double X) { return X <= 0 ? FSmvI18n::T(TEXT("power.default")).ToString() : FString::Printf(TEXT("%.0f m/s · %.0f km/h"), X, X * 3.6); }), Tx(TEXT("hw.max_speed_hint")))];
	if (D.Type != TEXT("rover"))
		V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hw.max_alt")), Slider(&D.Equipment.MaxAltM, -1, 1500, 5, [](double X) { return X <= 0 ? FSmvI18n::T(TEXT("power.default")).ToString() : FString::Printf(TEXT("%.0f m"), X); }), Tx(TEXT("hw.max_alt_hint")))];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(FSmvI18n::Format(TEXT("hw.type_params"), { { TEXT("t"), FSmvI18n::T(FString(TEXT("type.")) + D.Type).ToString() } }), Accent2())];
	for (const FYhParam& P : Defs)
	{
		double* Ptr = D.Params.Find(P.Name);
		if (!Ptr) continue;
		const FString Nm = P.Name;
		V->AddSlot().AutoHeight().Padding(0, 3)[Row(FText::FromString(FSmvI18n::T(P.Key).ToString() + TEXT("  ·  ") + Nm), Slider(Ptr, P.Min, P.Max, P.Step, [](double X) { return FString::SanitizeFloat(FMath::RoundToDouble(X * 100.0) / 100.0); }))];
	}
	// tüm kayıtlı parametreler (Mission Planner'dan gelenler dahil) + yeni parametre
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(FSmvI18n::Format(TEXT("hw.all_params"), { { TEXT("n"), FString::FromInt(D.Params.Num()) } }), Warn())];
	TArray<FString> Keys;
	D.Params.GetKeys(Keys);
	Keys.Sort();
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).PreferredSize(560.f).UseAllottedSize(true);
	for (const FString& K : Keys)
	{
		const double Val = D.Params[K];
		Wrap->AddSlot().Padding(0, 0, 6, 6)
		[
			SNew(SBorder).BorderImage(Round(8.f)).BorderBackgroundColor(Card()).Padding(FMargin(8.f, 3.f, 3.f, 3.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s = %s"), *K, *FString::SanitizeFloat(Val))), 10, "Mono", Fg(), false)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
				[
					SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Trash).FontSize(10).OnClick([Self, K]() { Self->EditDrone.Params.Remove(K); Self->Refresh(); })
				]
			]
		];
	}
	V->AddSlot().AutoHeight()[Wrap];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[TextEdit(&HwParamName, false, Tx(TEXT("hw.param_name")), 200.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[TextEdit(&HwParamValue, false, Tx(TEXT("hw.param_value")), 110.f)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Normal).Icon(ESmvIcon::Plus).Text(Tx(TEXT("hw.param_add")))
			.OnClick([Self]()
			{
				const FString N = Self->HwParamName.TrimStartAndEnd().ToUpper();
				if (N.IsEmpty() || N.Len() > 16 || !Self->HwParamValue.IsNumeric()) { Self->Toast(FSmvI18n::T(TEXT("hw.param_bad")).ToString(), 4); return; }
				if (N.StartsWith(TEXT("SIM_"))) { Self->Toast(FSmvI18n::T(TEXT("hw.param_sim")).ToString(), 4); return; }
				Self->EditDrone.Params.Add(N, FCString::Atod(*Self->HwParamValue));
				Self->HwParamName.Empty(); Self->HwParamValue.Empty();
				Self->Refresh();
			})]
	];
	// Lua betiği
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(Tx(TEXT("hw.script")), Accent3())];
	V->AddSlot().AutoHeight()
	[
		SNew(SBox).HeightOverride(170.f)
		[
			SNew(SMultiLineEditableTextBox)
			.Font(Font(11, "Mono"))
			.Text(FText::FromString(D.Script))
			.HintText(Tx(TEXT("hw.script_hint")))
			.AutoWrapText(false)
			.OnTextChanged_Lambda([Self](const FText& T) { Self->EditDrone.Script = T.ToString(); })
		]
	];
	V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SmvUi::Text(Tx(TEXT("hangar2.mp_hint")), 10, "Regular", Mute())];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(640.f)
			[
				SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[Glass(V, FMargin(20.f), 0.88f)]
			]
		];
}

TSharedRef<SWidget> SSmvShell::HwCamera()
{
	SSmvShell* Self = this;
	struct FC { const TCHAR* Id; ESmvIcon Icon; uint32 Col; };
	static const FC Cams[] = { { TEXT("day"), ESmvIcon::Sun, 0xfbbf24 }, { TEXT("night"), ESmvIcon::Moon, 0x4ade80 }, { TEXT("thermal"), ESmvIcon::Bolt, 0xf97316 }, { TEXT("multispectral"), ESmvIcon::Globe, 0xe879f9 } };
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	int32 i = 0;
	for (const FC& C : Cams)
	{
		const FString Id = C.Id;
		const bool bSel = EditDrone.Equipment.Camera == Id;
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Hex(C.Col)).MinWidth(280.f).MinHeight(170.f).Selected(bSel).Icon(C.Icon).IntroDelay(0.05f * i++)
			.Text(Tx(*(FString(TEXT("cam2.")) + Id))).SubText(Tx(*(FString(TEXT("cam2.")) + Id + TEXT("_d"))))
			.OnClick([Self, Id]() { Self->EditDrone.Equipment.Camera = Id; Self->Refresh(); });
		if (bSel) DefaultFocus = B;
		Row->AddSlot().AutoWidth().Padding(0, 0, 12, 0)[B];
	}
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)
	[
		SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Hex(0x0b1222, 0.7f)).Padding(FMargin(14.f, 8.f))
		[
			SmvUi::Text(Tx(TEXT("hw.camera_live")), 12, "Regular", Fg(), false)
		]
	];
	if (EditDrone.Equipment.Camera == TEXT("thermal"))
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[Chips(&FSmvUiPrefs::Get().ThermalPalette, { TEXT("white"), TEXT("black"), TEXT("iron") },
			{ Tx(TEXT("camera.pal_white")), Tx(TEXT("camera.pal_black")), Tx(TEXT("camera.pal_iron")) }, {}, []() { FSmvUiPrefs::Get().Save(); }, 3)];
	V->AddSlot().AutoHeight().HAlign(HAlign_Center)[Row];
	return SNew(SBox).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(0, 0, 0, 6))[V];
}

TSharedRef<SWidget> SSmvShell::HwAddons()
{
	SSmvShell* Self = this;
	FSmvEquipment& E = EditDrone.Equipment;
	const FString Ty = EditDrone.Type;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Card = [&V, Ty](const TCHAR* Id, ESmvIcon Ic, uint32 Col, bool* On, TSharedRef<SWidget> Opts, SSmvShell* Sh)
	{
		const bool bFit = FSmvEquipment::AddonFits(Id, Ty);
		TSharedRef<SVerticalBox> In = SNew(SVerticalBox);
		In->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Icon(Ic, 30.f, bFit ? Hex(Col) : Mute())]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(YhT(FString(TEXT("addon.")) + Id), 15, "Bold", bFit ? Fg() : Mute(), false)]
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(bFit ? YhT(FString(TEXT("addon.")) + Id + TEXT("_d")) : YhT(TEXT("addon.not_fit")), 10, "Regular", Dim())]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				bFit ? Sh->Toggle(On, [Sh]() { Sh->Refresh(); }) : SNullWidget::NullWidget
			]
		];
		if (bFit && *On) In->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Opts];
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[Glass(In, FMargin(16.f), 0.9f)];
	};
	auto Fmt = [](const TCHAR* Unit) { const FString U(Unit); return [U](double X) { return FString::Printf(TEXT("%.1f %s"), X, *U); }; };
	Card(TEXT("sprayer"), ESmvIcon::Rain, 0x34d399, &E.bSprayer,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("addon.tank_l")), Slider(&E.SprayTankL, 1, 40, 1, Fmt(TEXT("L"))))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("addon.width_m")), Slider(&E.SprayWidthM, 1, 12, 0.5, Fmt(TEXT("m"))))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("addon.pump_rate")), Slider(&E.SprayPumpRate, 0, 100, 1, Fmt(TEXT("%"))))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(FText::FromString(TEXT("SPRAY_ENABLE=1 · SERVO9_FUNCTION=22 · SERVO10_FUNCTION=23")), 9, "Mono", Mute(), false)], Self);
	Card(TEXT("gripper"), ESmvIcon::Next, 0xfbbf24, &E.bGripper,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("addon.payload_kg")), Slider(&E.PayloadKg, 0.1, 20, 0.1, Fmt(TEXT("kg"))))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(FText::FromString(TEXT("GRIP_ENABLE=1 · GRIP_TYPE=1 · SERVO11_FUNCTION=28")), 9, "Mono", Mute(), false)], Self);
	Card(TEXT("parachute"), ESmvIcon::Ghost, 0xf87171, &E.bParachute,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[Row(Tx(TEXT("addon.alt_min_m")), Slider(&E.ChuteAltMinM, 0, 100, 1, Fmt(TEXT("m"))))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[SmvUi::Text(FText::FromString(TEXT("CHUTE_ENABLED=1 · CHUTE_TYPE=10 · SERVO12_FUNCTION=27")), 9, "Mono", Mute(), false)], Self);
	V->AddSlot().AutoHeight().Padding(4, 2, 0, 0)[SmvUi::Text(Tx(TEXT("addon.keys")), 10, "Regular", Dim())];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(560.f)[SNew(SScrollBox) + SScrollBox::Slot()[V]]
		];
}

TSharedRef<SWidget> SSmvShell::HwLook()
{
	SSmvShell* Self = this;
	FSmvEquipment& E = EditDrone.Equipment;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("hw.identity")), Accent())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hangar.name")), TextEdit(&EditDrone.Name, false, FText::GetEmpty(), 260.f))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hw.callsign")), TextEdit(&E.Callsign, false, FText::FromString(TEXT("TC-001")), 140.f), Tx(TEXT("hw.callsign_hint")))];
	V->AddSlot().AutoHeight().Padding(0, 12, 0, 6)[Section(Tx(TEXT("hw.paint")), Warn())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hangar.color")), ColorPick(&EditDrone.Color, [Self]() { Self->Refresh(); }))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hw.color2")), ColorPick(&E.Color2, [Self]() { Self->Refresh(); }), Tx(TEXT("hw.color2_hint")))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hw.finish")), Chips(&E.Finish, { TEXT("gloss"), TEXT("matte"), TEXT("metal"), TEXT("carbon") },
		{ Tx(TEXT("finish.gloss")), Tx(TEXT("finish.matte")), Tx(TEXT("finish.metal")), Tx(TEXT("finish.carbon")) }, {}, [Self]() { Self->Refresh(); }, 4))];
	V->AddSlot().AutoHeight().Padding(0, 12, 0, 6)[Section(FSmvI18n::Format(TEXT("hw.decals"), { { TEXT("n"), FString::FromInt(E.Decals.Num()) } }), Accent2())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hw.decal_slot")), Chips(&DecalSlot, { TEXT("top"), TEXT("left"), TEXT("right"), TEXT("front"), TEXT("rear"), TEXT("bottom"), TEXT("tail") },
		{ Tx(TEXT("slot.top")), Tx(TEXT("slot.left")), Tx(TEXT("slot.right")), Tx(TEXT("slot.front")), Tx(TEXT("slot.rear")), Tx(TEXT("slot.bottom")), Tx(TEXT("slot.tail")) }, {}, nullptr, 4))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("hw.decal_scale")), Slider(&DecalScale, 0.2, 2.0, 0.1, [](double X) { return FString::Printf(TEXT("%.1f×"), X); }))];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 6)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Normal).Icon(ESmvIcon::Plus).Text(Tx(TEXT("hw.decal_pick")))
			.IsEnabled(E.Decals.Num() < 6)
			.OnClick([Self]()
			{
				FString Src;
				if (!YhPickPng(Src)) { Self->Toast(FSmvI18n::T(TEXT("hw.decal_none")).ToString(), 4); return; }
				const FString Dst = YhImportDecal(Src);
				if (Dst.IsEmpty()) { Self->Toast(FSmvI18n::T(TEXT("hw.decal_bad")).ToString(), 3); return; }
				FSmvDecal Dc;
				Dc.File = Dst; Dc.Slot = Self->DecalSlot; Dc.Scale = Self->DecalScale; Dc.Kind = TEXT("sticker");
				Self->EditDrone.Equipment.Decals.Add(Dc);
				Self->Refresh();
			})]
	];
	for (int32 i = 0; i < E.Decals.Num(); ++i)
	{
		const FSmvDecal& Dc = E.Decals[i];
		V->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SBorder).BorderImage(Round(8.f)).BorderBackgroundColor(Card()).Padding(FMargin(10.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %s · %.1f×"), *FPaths::GetCleanFilename(Dc.File).Left(18),
					*FSmvI18n::T(FString(TEXT("slot.")) + Dc.Slot).ToString(), Dc.Scale)), 11, "Regular", Fg(), false)]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Trash).FontSize(10).OnClick([Self, i]()
				{
					if (Self->EditDrone.Equipment.Decals.IsValidIndex(i)) Self->EditDrone.Equipment.Decals.RemoveAt(i);
					Self->Refresh();
				})]
			]
		];
	}
	V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SmvUi::Text(Tx(TEXT("hw.decal_hint")), 10, "Regular", Mute())];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(600.f)[SNew(SScrollBox) + SScrollBox::Slot()[Glass(V, FMargin(20.f), 0.88f)]]
		];
}

void SSmvShell::HwSave()
{
	if (!Data()) return;
	SSmvShell* Self = this;
	FSmvDrone& D = EditDrone;
	D.Name = D.Name.TrimStartAndEnd().Left(40);
	if (D.Name.IsEmpty()) { Toast(FSmvI18n::T(TEXT("hw.name_required")).ToString(), 4); HwStep = 5; Refresh(); return; }
	D.Equipment.Callsign = D.Equipment.Callsign.TrimStartAndEnd().ToUpper().Left(8);
	// azami hız → tipin parametreleri (Copter WPNAV_SPEED/LOIT_SPEED cm/s, Plane AIRSPEED_MAX m/s, Rover WP_SPEED/CRUISE_SPEED)
	const bool bPlane = D.Type == TEXT("plane") || D.Type == TEXT("vtol") || D.Type == TEXT("interceptor");
	if (HwMaxSpeed > 0)
	{
		if (D.Type == TEXT("rover")) { D.Params.Add(TEXT("WP_SPEED"), HwMaxSpeed); D.Params.Add(TEXT("CRUISE_SPEED"), FMath::RoundToDouble(HwMaxSpeed * 0.8 * 10.0) / 10.0); }
		else if (bPlane) { D.Params.Add(TEXT("AIRSPEED_MAX"), HwMaxSpeed); D.Power.MaxSpeedMs = HwMaxSpeed; }
		else { D.Params.Add(TEXT("WPNAV_SPEED"), HwMaxSpeed * 100.0); D.Params.Add(TEXT("LOIT_SPEED"), HwMaxSpeed * 100.0); }
	}
	const bool bWasNew = D.Id.IsEmpty();
	Data()->SaveDrone(D, FSmvDone::CreateLambda([Self, bWasNew](bool bOk, const FString& M)
	{
		if (!bOk) { Self->Toast(M, 3); return; }
		if (bWasNew && Self->Data()->Drones().Num()) Self->EditDrone = Self->Data()->Drones().Last();
		if (ASmvMenuScene* Sc = Self->Scene()) Sc->PlayLanding();
		Self->Toast(bWasNew ? Self->EditDrone.Name + TEXT(" — ") + FSmvI18n::T(TEXT("hangar.landed")).ToString() : FSmvI18n::T(TEXT("common.saved")).ToString(), 6);
		Self->bHwEdit = false;
		if (Self->Flow.IsValid()) Self->Flow->SetScreen(ESmvScreen::Hangar);
	}));
}
