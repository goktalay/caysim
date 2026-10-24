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
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
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

	/** Pilot rütbe simgesi (Fab "2D Icons – 150 Space Rank"; /Game/150SpaceRank/PNG/128): seviye 1–100 → 150 rütbe sırasına yayılır.
	 *  Paket yoksa nullptr (seviye rakamı kalır). */
	const FSlateBrush* YhRankBrush(int32 Level, float Size)
	{
		static TArray<UTexture2D*> Tex;
		static bool bRead = false;
		static TMap<int32, TSharedPtr<FSlateBrush>> Cache;
		if (!bRead)
		{
			bRead = true;
			TArray<FAssetData> A;
			IAssetRegistry::GetChecked().GetAssetsByPath(FName(TEXT("/Game/150SpaceRank/PNG/128")), A, true);
			auto Num = [](const FAssetData& X)
			{
				const FString N = X.AssetName.ToString();
				int32 i = N.Len();
				while (i > 0 && FChar::IsDigit(N[i - 1])) --i;
				return i < N.Len() ? FCString::Atoi(*N.Mid(i)) : 0;
			};
			A.Sort([&](const FAssetData& X, const FAssetData& Y) { const int32 a = Num(X), b = Num(Y); return a != b ? a < b : X.AssetName.LexicalLess(Y.AssetName); });
			for (const FAssetData& X : A)
				if (X.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName())
					if (UTexture2D* T = Cast<UTexture2D>(X.GetAsset())) { T->AddToRoot(); Tex.Add(T); }
		}
		if (Tex.Num() == 0) return nullptr;
		const int32 Ix = FMath::Clamp((int32)FMath::RoundToInt((FMath::Clamp(Level, 1, 100) - 1) * (Tex.Num() - 1) / 99.0), 0, Tex.Num() - 1);
		const int32 Key = Ix * 1000 + (int32)Size;
		if (TSharedPtr<FSlateBrush>* B = Cache.Find(Key)) return B->Get();
		TSharedPtr<FSlateBrush> B = MakeShared<FSlateBrush>();
		B->SetResourceObject(Tex[Ix]);
		B->ImageSize = FVector2D(Size, Size);
		B->DrawAs = ESlateBrushDrawType::Image;
		Cache.Add(Key, B);
		return B.Get();
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
		const FString T = SmvUi::SmvUpper(Title.ToString());
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
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, YhRankBrush(Lv, 18.f) ? 6.f : 0.f, 0)
					[
						YhRankBrush(Lv, 18.f) ? YhW<SImage>(SNew(SImage).Image(YhRankBrush(Lv, 18.f))) : SNullWidget::NullWidget
					]
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
			.OnClick([Self, F]() { if (!F.IsValid()) return; F->NewDraft(); F->Draft.Drone = Self->EditDrone; F->FlyStep = 1; F->SetScreen(ESmvScreen::FlyFlow); })]
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
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(130.f)[SmvUi::Text(K, 11, "Regular", Dim(), false)]]
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

// ---------------------------------------------------------------------------
// PARKUR: üç animasyonlu seçenek (Karşılaşma · Özel · Gerçek Dünya) → liste → sihirbazlar
// ---------------------------------------------------------------------------

namespace
{
	/** Görsel döngüsü (Ken Burns): görseller sırayla yumuşak geçişle değişir, her biri yavaşça yakınlaşır */
	TSharedRef<SWidget> YhSlides(const TArray<const FSlateBrush*>& Imgs, float W, float H, double Period)
	{
		TSharedRef<SOverlay> O = SNew(SOverlay);
		const double Born = FPlatformTime::Seconds();
		const int32 N = Imgs.Num();
		for (int32 i = 0; i < N; ++i)
		{
			O->AddSlot()
			[
				SNew(SImage).Image(Imgs[i])
				.ColorAndOpacity_Lambda([i, N, Born, Period]()
				{
					if (N <= 1) return FSlateColor(FLinearColor::White);
					const double T = (FPlatformTime::Seconds() - Born) / Period;
					const int32 Cur = (int32)FMath::Fmod(FMath::FloorToDouble(T), (double)N);
					const int32 Nx = (Cur + 1) % N;
					const double Fr = T - FMath::FloorToDouble(T);
					const float S = (float)FMath::SmoothStep(0.78, 1.0, Fr);
					float A = 0.f;
					if (i == Cur) A = Nx > Cur ? 1.f : 1.f - S;
					else if (i == Nx) A = Nx > Cur ? S : 1.f;
					return FSlateColor(FLinearColor(1.f, 1.f, 1.f, A));
				})
				.RenderTransformPivot(FVector2D(0.5f, 0.5f))
				.RenderTransform_Lambda([i, Born, Period]()
				{
					if (ReducedMotion()) return TOptional<FSlateRenderTransform>();
					const double T = (FPlatformTime::Seconds() - Born) / Period;
					const float Z = 1.04f + 0.05f * (float)FMath::Sin(T * 0.9 + i * 1.7);
					return TOptional<FSlateRenderTransform>(FSlateRenderTransform(Z, FVector2f(6.f * (float)FMath::Sin(T * 0.6 + i), 0.f)));
				})
			];
		}
		return SNew(SBox).WidthOverride(W).HeightOverride(H).Clipping(EWidgetClipping::ClipToBounds)[O];
	}

	/** Biyom resmi (Özel parkur kartları): katmanlı dağ siluetleri, orman / şehir silueti, su, gök degradesi; T > 0 iken yavaş paralaks.
	 *  Bi: 0 orman, 1 şehir, 2 dağlık */
	void YhPaintBiome(FSlateWindowElementList& Out, int32 L, const FGeometry& G, int32 Bi, uint32 Seed, double T)
	{
		const FVector2D S = FVector2D(G.GetLocalSize());
		static const uint32 SkyTop[3] = { 0x1d4e5f, 0x312e81, 0x1e3a8a };
		static const uint32 SkyLow[3] = { 0xf6c27a, 0xf59e0b, 0xbfdbfe };
		DrGradient(Out, L, G, FVector2D::ZeroVector, S, { { 0.f, Hex(SkyTop[Bi]) }, { 0.62f, Hex(SkyLow[Bi], 0.85f) }, { 1.f, Hex(0x0b1222) } }, true);
		Out.PushClip(FSlateClippingZone(G));
		DrGlow(Out, L + 1, G, FVector2D(S.X * 0.72f, S.Y * 0.5f), (float)S.Y * 0.42f, Hex(0xfff1c1, 0.22f));
		const float Hz = (float)S.Y * 0.64f;
		auto Ridge = [&Out, &G, &S, Seed, T](int32 Layer, float Base, float Amp, float Freq, const FLinearColor& C, double Speed)
		{
			const float Bw = 3.f;
			const int32 Nb = (int32)(S.X / Bw) + 1;
			for (int32 k = 0; k < Nb; ++k)
			{
				const double X = k * Bw + T * Speed;
				const double Nz = 0.55 * FMath::Sin(X * 0.011 * Freq + Seed * 0.13) + 0.3 * FMath::Sin(X * 0.031 * Freq + Seed * 0.71) + 0.15 * FMath::Sin(X * 0.083 * Freq);
				const float Top = Base - Amp * (float)(0.5 + 0.5 * Nz);
				DrBox(Out, Layer, G, FVector2D(k * Bw, Top), FVector2D(Bw + 0.6f, (float)S.Y - Top), C, nullptr);
			}
		};
		if (Bi == 2)
		{
			Ridge(L + 2, Hz - 6.f, (float)S.Y * 0.42f, 0.8f, Hex(0x64748b, 0.9f), 2.0);
			// karlı tepeler: arka sırtın üst çizgisi
			Ridge(L + 3, Hz + 10.f, (float)S.Y * 0.3f, 1.3f, Hex(0x334155), 5.0);
			Ridge(L + 4, Hz + 34.f, (float)S.Y * 0.16f, 2.2f, Hex(0x1e293b), 9.0);
		}
		else
		{
			Ridge(L + 2, Hz, (float)S.Y * 0.16f, 0.7f, Hex(Bi == 1 ? 0x4c4f7a : 0x3f6b5a, 0.85f), 2.0);
		}
		// su (göl / deniz şeridi; parıltı çizgileri)
		const float Wy = (float)S.Y * 0.80f;
		DrBox(Out, L + 5, G, FVector2D(0.f, Wy), FVector2D((float)S.X, (float)S.Y - Wy), Hex(0x0e3a5c, 0.95f), nullptr);
		for (int32 k = 0; k < 7; ++k)
		{
			const float Px = (float)FMath::Fmod(k * 53.0 + T * (12.0 + k), S.X + 60.0) - 30.f;
			DrBox(Out, L + 6, G, FVector2D(Px, Wy + 6.f + k * 4.f), FVector2D(18.f + 6.f * (k % 3), 1.5f), Hex(0xfde68a, 0.25f), nullptr);
		}
		if (Bi == 0 || Bi == 2)
		{
			// orman: koyu iğne yapraklı ağaç siluetleri (gövde + üç kat taç)
			const int32 Nt = Bi == 0 ? 38 : 16;
			for (int32 k = 0; k < Nt; ++k)
			{
				const uint32 Hk = (Seed * 2654435761u) ^ (k * 2246822519u);
				const float X = (float)FMath::Fmod((Hk % 1000) / 1000.0 * (S.X + 40.0) + T * 14.0, S.X + 40.0) - 20.f;
				const float Hh = 16.f + (float)((Hk >> 10) % 26);
				const float By = Wy + 2.f - (float)((Hk >> 20) % 6);
				const FLinearColor Tc = Hex(Bi == 0 ? 0x0f2e1f : 0x13241c, 0.98f);
				DrBox(Out, L + 7, G, FVector2D(X - 1.f, By - Hh * 0.3f), FVector2D(2.f, Hh * 0.3f), Tc, nullptr);
				for (int32 j = 0; j < 3; ++j)
				{
					const float Wd = Hh * (0.55f - j * 0.14f), Y0 = By - Hh * (0.3f + j * 0.22f);
					DrBox(Out, L + 7, G, FVector2D(X - Wd * 0.5f, Y0 - Hh * 0.26f), FVector2D(Wd, Hh * 0.28f), Tc, Round(Wd * 0.45f));
				}
			}
		}
		else
		{
			// şehir: kule silueti + ışıklı pencereler
			for (int32 k = 0; k < 26; ++k)
			{
				const uint32 Hk = (Seed * 2654435761u) ^ (k * 3266489917u);
				const float Bw = 14.f + (float)(Hk % 20);
				const float X = (float)FMath::Fmod(k * (S.X / 22.0) + T * 10.0, S.X + 40.0) - 20.f;
				const float Bh = 30.f + (float)((Hk >> 8) % 90);
				DrBox(Out, L + 7, G, FVector2D(X, Wy - Bh), FVector2D(Bw, Bh), Hex(0x111827, 0.97f), nullptr);
				for (int32 r = 0; r < (int32)(Bh / 9.f) - 1; ++r)
					for (int32 c = 0; c < (int32)(Bw / 6.f); ++c)
						if (((Hk >> ((r * 3 + c) % 29)) & 3u) == 0u)
							DrBox(Out, L + 8, G, FVector2D(X + 2.f + c * 6.f, Wy - Bh + 4.f + r * 9.f), FVector2D(3.f, 4.f), Hex(0xfde68a, 0.75f), nullptr);
			}
		}
		// ön plan kararması
		DrGradient(Out, L + 9, G, FVector2D(0.f, (float)S.Y * 0.6f), FVector2D((float)S.X, (float)S.Y * 0.4f), { { 0.f, FLinearColor(0, 0, 0, 0.f) }, { 1.f, FLinearColor(0, 0, 0, 0.45f) } }, true);
		Out.PopClip();
	}

	int32 YhBiomeIx(const FString& B) { return B == TEXT("city") ? 1 : B == TEXT("mountain") ? 2 : 0; }

	/** Biyom resmi bileşeni; Cycle: üç biyom 5 sn'de bir döner (Parkur merkezi Özel kartı yedeği) */
	TSharedRef<SWidget> YhBiomeArt(const FString& Biome, uint32 Seed, float W, float H, bool bAnim, bool bCycle = false)
	{
		TSharedRef<SSmvCanvas> C = SNew(SSmvCanvas).Size(FVector2D(W, H)).Visibility(EVisibility::HitTestInvisible);
		const double Born = FPlatformTime::Seconds();
		const int32 Bi0 = YhBiomeIx(Biome);
		C->Painter = [Born, Bi0, Seed, bAnim, bCycle](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			const double T = (bAnim && !ReducedMotion()) ? FPlatformTime::Seconds() - Born : 0.0;
			const int32 Bi = bCycle ? (int32)FMath::Fmod(T / 5.0, 3.0) : Bi0;
			YhPaintBiome(Out, L, G, Bi, Seed + (bCycle ? (uint32)Bi * 17u : 0u), T);
		};
		return C;
	}

	/** Biyom ön ayarları (Özel parkur): dünya üretici parametreleri */
	void YhApplyBiome(FSmvTrack& T, const FString& B)
	{
		FSmvTrackWorld& W = T.World;
		W.Biome = B;
		T.WorldMode = TEXT("procedural");
		T.ParkurKind = TEXT("custom");
		if (B == TEXT("forest")) { W.TreeDensity = 0.7; W.Water = 0.15; W.MountainCount = 2; W.MountainHeight = 120; W.BuildingCount = 4; W.Vehicles = 0; W.People = 0; }
		else if (B == TEXT("city")) { W.TreeDensity = 0.12; W.Water = 0.05; W.MountainCount = 0; W.MountainHeight = 0; W.BuildingCount = 180; W.Vehicles = 40; W.People = 60; }
		else { W.TreeDensity = 0.35; W.Water = 0.12; W.MountainCount = 7; W.MountainHeight = 650; W.BuildingCount = 6; W.Vehicles = 0; W.People = 0; }
	}

	uint32 YhBiomeColor(const FString& B) { return B == TEXT("city") ? 0x38bdf8 : B == TEXT("mountain") ? 0xe2e8f0 : 0x34d399; }
	ESmvIcon YhBiomeIcon(const FString& B) { return B == TEXT("city") ? ESmvIcon::Home : B == TEXT("mountain") ? ESmvIcon::Flag : ESmvIcon::Sun; }
	FString YhBiomeOf(const FSmvTrack& T)
	{
		if (!T.World.Biome.IsEmpty()) return T.World.Biome;
		return T.World.BuildingCount > 100 ? FString(TEXT("city")) : T.World.MountainCount >= 5 ? FString(TEXT("mountain")) : FString(TEXT("forest"));
	}
}

TSharedRef<SWidget> SSmvShell::ParkurHub()
{
	USmvDataService* D = Data();
	if (!D || !Flow.IsValid()) return SNullWidget::NullWidget;
	TWeakObjectPtr<USmvFlow> F = Flow;
	int32 NCustom = 0, NReal = 0;
	for (const FSmvTrack& Tr : D->Tracks()) { if (Tr.IsReal()) ++NReal; else if (Tr.IsCustom()) ++NCustom; }
	const int32 NEnc = FSmvFabContent::Maps().Num();
	const float CW = 430.f, CH = 250.f;
	ASmvMenuScene* Sc = Scene();
	const bool bLive3d = Sc && Sc->IsLive() && FSmvUiPrefs::Get().bPreview3d;
	// 1) Karşılaşma: menü sahnesinin gerçek Fab haritasından canlı ikinci görünüm (alçak yörünge); yoksa harita görselleri döngüsü
	TArray<const FSlateBrush*> Shots;
	for (const FSmvFabMap& M : FSmvFabContent::AllMaps()) if (const FSlateBrush* B = YhMapPreview(M)) Shots.Add(B);
	TSharedRef<SWidget> EncPic = Shots.Num() ? YhSlides(Shots, CW, CH, 4.5) : YhBiomeArt(TEXT("forest"), 7u, CW, CH, true, true);
	if (bLive3d)
		if (UTextureRenderTarget2D* Rt = Sc->GetCardTarget())
			if (const FSlateBrush* B = YhRtBrush(YhBrushes, Rt, FVector2D(640, 360)))
				EncPic = SNew(SBox).WidthOverride(CW).HeightOverride(CH)[SNew(SImage).Image(B)];
	// 2) Özel: canlı prosedürel dünya (orman + dağ + su; 3B önizleme yakalaması yavaş yörüngede)
	TSharedRef<SWidget> CusPic = YhBiomeArt(TEXT("forest"), 11u, CW, CH, true, true);
	if (bLive3d)
	{
		if (!bHubWorld)
		{
			FSmvTrack Sample;
			Sample.World.Seed = 2026; Sample.World.SizeM = 3000;
			YhApplyBiome(Sample, TEXT("mountain"));
			Sample.World.TreeDensity = 0.5; Sample.World.MountainHeight = 380;
			Sc->SetWorldPreview(Sample);
			bHubWorld = true;
		}
		if (Sc->GetPreviewTarget())
		{
			PreviewBrush.SetResourceObject(Sc->GetPreviewTarget());
			PreviewBrush.ImageSize = FVector2D(960, 600);
			PreviewBrush.SetUVRegion(FBox2f(FVector2f(0.f, 0.06f), FVector2f(1.f, 0.94f)));
			bPreviewVisible = true;
			CusPic = SNew(SBox).WidthOverride(CW).HeightOverride(CH)[SNew(SImage).Image(&PreviewBrush)];
		}
	}
	// 3) Gerçek Dünya: uydu görüntüsü (Esri) üzerinde yavaş kayan "uydu geçişi"
	TSharedRef<SWidget> RealPic = SatThumb(41.0082, 28.9784, CW, CH, true);
	struct FCard { const TCHAR* Cat; const TCHAR* Title; const TCHAR* Sub; uint32 Col; ESmvIcon Ic; int32 N; };
	const FCard Cards[3] = {
		{ TEXT("encounter"), TEXT("parkur.enc"), TEXT("parkur.enc_d"), 0xfbbf24, ESmvIcon::Match, NEnc },
		{ TEXT("custom"), TEXT("parkur.custom"), TEXT("parkur.custom_d"), 0x34d399, ESmvIcon::Map, NCustom },
		{ TEXT("real"), TEXT("parkur.real"), TEXT("parkur.real_d"), 0x38bdf8, ESmvIcon::Globe, NReal } };
	TSharedRef<SWidget> Pics[3] = { EncPic, CusPic, RealPic };
	TSharedRef<SHorizontalBox> Hb = SNew(SHorizontalBox);
	for (int32 i = 0; i < 3; ++i)
	{
		const FCard& Cd = Cards[i];
		const FString Cat = Cd.Cat;
		const bool bLiveBadge = (i == 0 && bLive3d) || (i == 1 && bLive3d);
		TSharedRef<SWidget> Pic = SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SBorder).BorderImage(Round(14.f)).BorderBackgroundColor(Hex(0x0b1222, 0.95f)).Padding(0.f)[Pics[i]]]
			// alt degrade: başlık okunur
			+ SOverlay::Slot().VAlign(VAlign_Bottom)[SNew(SBox).HeightOverride(90.f)[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.35f))]]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(12.f)
			[
				SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Hex(0x0b1222, 0.8f)).Padding(FMargin(8.f))[Icon(Cd.Ic, 26.f, Hex(Cd.Col))]
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(12.f)
			[
				SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Hex(0x0b1222, 0.8f)).Padding(FMargin(10.f, 4.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, bLiveBadge ? 6.f : 0.f, 0)
					[
						bLiveBadge ? YhW<SBox>(SNew(SBox).WidthOverride(7.f).HeightOverride(7.f)[SNew(SBorder).BorderImage(Circle())
							.BorderBackgroundColor_Lambda([]() { return FSlateColor(Danger().CopyWithNewOpacity(0.45f + 0.55f * (0.5f + 0.5f * FMath::Sin((float)FPlatformTime::Seconds() * 4.f)))); })])
						: SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SmvUi::Text(bLiveBadge ? Tx(TEXT("parkur.live")) : FSmvI18n::Format(TEXT("parkur.count"), { { TEXT("n"), FString::FromInt(Cd.N) } }), 10, "Bold", Fg(), false)
					]
				]
			];
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Hex(Cd.Col)).MinWidth(CW + 40.f).IntroDelay(0.08f * i)
			.Text(Tx(Cd.Title)).SubText(i == 0 ? Tx(Cd.Sub) : FSmvI18n::Format(Cd.Sub, { { TEXT("n"), FString::FromInt(Cd.N) } }))
			.OnClick([F, Cat]() { if (!F.IsValid()) return; F->ParkurCat = Cat; F->SetScreen(ESmvScreen::ParkurList); })
			.Extra()[SNew(SBox).WidthOverride(CW).HeightOverride(CH)[Pic]];
		if (i == 0) DefaultFocus = B;
		Hb->AddSlot().AutoWidth().Padding(0, 0, 16, 0)[B];
	}
	return YhPage(Tx(TEXT("menu2.parkur")), Tx(TEXT("parkur.sub")),
		SNew(SBox).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(0, 0, 0, 24))[Hb]);
}

void SSmvShell::OpenTrackChoice(const FSmvTrack& T)
{
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	const FSmvTrack Copy = T;
	TArray<FChoice> Ch;
	FChoice E1; E1.Label = YhT(TEXT("hangar2.edit")); E1.Icon = ESmvIcon::Gear; E1.Kind = 1;
	E1.Fn = [Self, F, Copy]()
	{
		Self->NewTrack = Copy;
		if (Copy.IsReal()) { Self->RwStep = 0; if (F.IsValid()) F->SetScreen(ESmvScreen::RealWizard); }
		else { Self->PwStep = 1; if (F.IsValid()) F->SetScreen(ESmvScreen::ParkurWizard); }
	};
	Ch.Add(E1);
	FChoice E2; E2.Label = YhT(TEXT("parkur.fly_here")); E2.Icon = ESmvIcon::Play;
	E2.Fn = [F, Copy]() { if (!F.IsValid()) return; F->NewDraft(); F->Draft.Track = Copy; F->ParkurCat = Copy.IsReal() ? TEXT("real") : TEXT("custom"); F->FlyStep = 3; F->SetScreen(ESmvScreen::FlyFlow); };
	Ch.Add(E2);
	FChoice E3; E3.Label = YhT(TEXT("parkur.advanced")); E3.Icon = ESmvIcon::Flag;
	E3.Fn = [Self, Copy]() { Self->EditTrack = Copy; Self->bTrackEditor = true; Self->Refresh(); };
	Ch.Add(E3);
	FChoice E4; E4.Label = YhT(TEXT("common.delete")); E4.Icon = ESmvIcon::Trash; E4.Kind = 2;
	E4.Fn = [Self, Copy]()
	{
		Self->Confirm(FSmvI18n::T(TEXT("dlg.delete_title")), FSmvI18n::Format(TEXT("dlg.delete_body"), { { TEXT("n"), Copy.Name } }), FSmvI18n::T(TEXT("dlg.yes_delete")), true, [Self, Copy]()
		{
			Self->Data()->DeleteTrack(Copy.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& Msg)
			{
				Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : Msg, bOk ? 6 : 3);
				Self->Refresh();
			}));
		});
	};
	Ch.Add(E4);
	OpenChoice(FText::FromString(Copy.Name), YhT(TEXT("parkur.ask")), Ch);
}

TSharedRef<SWidget> SSmvShell::ParkurList()
{
	USmvDataService* D = Data();
	if (!D || !Flow.IsValid()) return SNullWidget::NullWidget;
	if (bTrackEditor) return Tracks();
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	const FString Cat = Flow->ParkurCat;
	// ---- Karşılaşma: Fab haritaları; kart tıklanınca harita içinden kamera turu (yalnız izleme) ----
	if (Cat == TEXT("encounter"))
	{
		const TArray<FSmvFabMap>& All = FSmvFabContent::AllMaps();
		if (!TourKey.IsEmpty())
		{
			const FSmvFabMap* Cur = nullptr;
			int32 Ix = 0;
			TArray<FString> Keys;
			for (const FSmvFabMap& M : All) if (M.bAvailable && M.Kind == ESmvMapKind::Level) { if (M.Key == TourKey) { Cur = &M; Ix = Keys.Num(); } Keys.Add(M.Key); }
			const FText Nm = Cur ? Cur->Name : FText::FromString(TourKey);
			const FText Ds = Cur ? Cur->Description : FText::GetEmpty();
			auto Step = [Self, Keys, Ix](int32 Dir) { return [Self, Keys, Ix, Dir]() { if (Keys.Num()) { Self->TourKey = Keys[(Ix + Dir + Keys.Num()) % Keys.Num()]; Self->Refresh(); } }; };
			TWeakObjectPtr<ASmvMenuScene> WS(Scene());
			TSharedRef<SSmvButton> NextB = SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Next).Text(Tx(TEXT("parkur.tour_next"))).OnClick(Step(1));
			DefaultFocus = NextB;
			TSharedRef<SWidget> Card = Glass(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(Tx(TEXT("parkur.tour_badge")), 10, "Bold", Hex(0xfbbf24), false)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[Title(Nm, 30)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SNew(SBox).WidthOverride(560.f)[SmvUi::Text(Ds, 12, "Regular", Dim())]]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SmvUi::Text(TAttribute<FText>::CreateLambda([WS]() { return WS.IsValid() && !WS->IsMapReady() ? FSmvI18n::T(TEXT("parkur.tour_loading")) : FSmvI18n::T(TEXT("parkur.tour_hint")); }), 10, "Regular", Mute(), false)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[SNew(SSmvButton).Icon(ESmvIcon::Prev).Text(Tx(TEXT("parkur.tour_prev"))).OnClick(Step(-1))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[NextB]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Back).Text(Tx(TEXT("parkur.tour_close"))).OnClick([Self]() { Self->TourKey.Empty(); Self->Refresh(); })]
				], FMargin(22.f), 0.82f);
			return YhPage(Tx(TEXT("parkur.enc")), FSmvI18n::Format(TEXT("parkur.tour_of"), { { TEXT("i"), FString::FromInt(Ix + 1) }, { TEXT("n"), FString::FromInt(Keys.Num()) } }),
				SNew(SBox).VAlign(VAlign_Bottom).HAlign(HAlign_Left).Padding(FMargin(0, 0, 0, 10))[Card],
				[Self]() { Self->TourKey.Empty(); Self->Refresh(); });
		}
		TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).PreferredSize(1500.f).UseAllottedSize(true);
		int32 N = 0;
		for (const FSmvFabMap& M : All)
		{
			if (M.Kind != ESmvMapKind::Level) continue;
			const FString Key = M.Key;
			const bool bAv = M.bAvailable;
			const FSlateBrush* Img = YhMapPreview(M);
			TSharedRef<SWidget> Pic = Img ? YhW<SImage>(SNew(SImage).Image(Img).ColorAndOpacity(FLinearColor(1, 1, 1, bAv ? 1.f : 0.35f)))
				: YhW<SBox>(SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[Icon(ESmvIcon::Map, 40.f, Mute())]);
			TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Hex(0xfbbf24)).MinWidth(330.f).IntroDelay(0.04f * N)
				.IsEnabled(bAv)
				.Text(M.Name).SubText(bAv ? M.Description : FSmvI18n::Format(TEXT("parkur.missing_pkg"), { { TEXT("p"), M.Package } }))
				.OnClick([Self, Key]() { Self->TourKey = Key; Self->Refresh(); })
				.Extra()
				[
					SNew(SBox).WidthOverride(296.f).HeightOverride(166.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Hex(0x0b1222, 0.9f)).Padding(0.f)[Pic]]
						+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(8.f)
						[
							bAv ? YhW<SBorder>(SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Hex(0x0b1222, 0.85f)).Padding(FMargin(8.f, 3.f))
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)[Icon(ESmvIcon::Camera, 12.f, Hex(0xfbbf24))]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(Tx(TEXT("parkur.watch")), 10, "Bold", Fg(), false)]
								])
							: SNullWidget::NullWidget
						]
					]
				];
			if (N == 0 || (!DefaultFocus.IsValid() && bAv)) DefaultFocus = B;
			Wrap->AddSlot().Padding(0, 0, 14, 14)[SNew(SBox).WidthOverride(328.f)[B]];
			++N;
		}
		if (N == 0) Wrap->AddSlot()[Glass(SmvUi::Text(Tx(TEXT("parkur.enc_empty")), 13, "Regular", Dim()), FMargin(20.f), 0.9f)];
		return YhPage(Tx(TEXT("parkur.enc")), Tx(TEXT("parkur.enc_sub")),
			SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot().Padding(0, 6, 0, 0)[Wrap]);
	}
	// ---- Özel / Gerçek Dünya: kayıtlı parkurlar + "+ Yeni" ----
	const bool bReal = Cat == TEXT("real");
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).PreferredSize(1500.f).UseAllottedSize(true);
	int32 N = 0;
	for (const FSmvTrack& X : D->Tracks())
	{
		if (bReal ? !X.IsReal() : !X.IsCustom()) continue;
		const FSmvTrack Copy = X;
		TSharedRef<SWidget> Pic = SNullWidget::NullWidget;
		const FString Bi = YhBiomeOf(X);
		if (bReal) Pic = SatThumb(X.Lat, X.Lon, 296.f, 166.f, false);
		else Pic = YhBiomeArt(Bi, (uint32)X.World.Seed, 296.f, 166.f, false);
		const FString Info = bReal
			? FString::Printf(TEXT("%.4f, %.4f · %.1f km · %s"), X.Lat, X.Lon, X.RadiusM * 2.0 / 1000.0, *FSmvI18n::T(FString(TEXT("weather.")) + X.Weather).ToString())
			: FString::Printf(TEXT("%s · %s · %s"), *FSmvI18n::T(FString(TEXT("biome.")) + Bi).ToString(), *FSmvI18n::T(FString(TEXT("time.")) + X.TimeOfDay).ToString(), *FSmvI18n::T(FString(TEXT("weather.")) + X.Weather).ToString());
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(bReal ? Hex(0x38bdf8) : Hex(YhBiomeColor(Bi))).MinWidth(330.f).IntroDelay(0.04f * N)
			.Text(FText::FromString(X.Name)).SubText(FText::FromString(Info))
			.OnClick([Self, Copy]() { Self->OpenTrackChoice(Copy); })
			.Extra()
			[
				SNew(SBox).WidthOverride(296.f).HeightOverride(166.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Hex(0x0b1222, 0.9f)).Padding(0.f)[Pic]]
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.f)
					[
						SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Hex(0x0b1222, 0.85f)).Padding(FMargin(6.f))[Icon(bReal ? ESmvIcon::Globe : YhBiomeIcon(Bi), 16.f, bReal ? Hex(0x38bdf8) : Hex(YhBiomeColor(Bi)))]
					]
				]
			];
		if (N == 0) DefaultFocus = B;
		Wrap->AddSlot().Padding(0, 0, 14, 14)[SNew(SBox).WidthOverride(328.f)[B]];
		++N;
	}
	TSharedRef<SSmvButton> NewB = SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Plus).Text(Tx(TEXT("hangar2.new")))
		.OnClick([Self, F, bReal]()
		{
			FSmvTrack T;
			T.Name = FSmvI18n::T(bReal ? TEXT("parkur.real_new_name") : TEXT("parkur.custom_new_name")).ToString();
			T.World.Seed = FMath::RandRange(1, 999999);
			if (bReal) { T.ParkurKind = TEXT("real"); T.WorldMode = TEXT("real"); T.RadiusM = 1000; T.Lat = 41.0082; T.Lon = 28.9784; Self->RwStep = 0; Self->RealQuery.Empty(); }
			else { YhApplyBiome(T, TEXT("forest")); T.GroundMsl = 0; Self->PwStep = 0; }
			Self->NewTrack = T;
			if (F.IsValid()) F->SetScreen(bReal ? ESmvScreen::RealWizard : ESmvScreen::ParkurWizard);
		});
	if (N == 0)
	{
		DefaultFocus = NewB;
		Wrap->AddSlot()
		[
			Glass(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Icon(bReal ? ESmvIcon::Globe : ESmvIcon::Map, 40.f, bReal ? Hex(0x38bdf8) : Accent3())]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 4)[Title(Tx(bReal ? TEXT("parkur.real_empty") : TEXT("parkur.custom_empty")), 20)]
				+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(460.f)[SmvUi::Text(Tx(bReal ? TEXT("parkur.real_empty_d") : TEXT("parkur.custom_empty_d")), 12, "Regular", Dim())]],
				FMargin(26.f), 0.88f)
		];
	}
	return YhPage(Tx(bReal ? TEXT("parkur.real") : TEXT("parkur.custom")), Tx(bReal ? TEXT("parkur.real_sub") : TEXT("parkur.custom_sub")),
		SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot().Padding(0, 6, 0, 0)[Wrap], nullptr, NewB);
}

TSharedRef<SWidget> SSmvShell::EnvEditor(FSmvTrack& T)
{
	SSmvShell* Self = this;
	auto Ms = [](double V) { return FString::Printf(TEXT("%.1f m/s"), V); };
	auto Deg = [](double V) { return FString::Printf(TEXT("%.0f°"), V); };
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("parkur.s_weather")), Warn())];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.time")), Chips(&T.TimeOfDay, { TEXT("day"), TEXT("golden"), TEXT("dusk"), TEXT("night") },
		{ Tx(TEXT("time.day")), Tx(TEXT("time.golden")), Tx(TEXT("time.dusk")), Tx(TEXT("time.night")) }, { ESmvIcon::Sun, ESmvIcon::Sunset, ESmvIcon::Sunset, ESmvIcon::Moon }, [Self]() { Self->Refresh(); }, 4))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.weather")), Chips(&T.Weather, { TEXT("clear"), TEXT("cloudy"), TEXT("rain"), TEXT("fog"), TEXT("snow") },
		{ Tx(TEXT("weather.clear")), Tx(TEXT("weather.cloudy")), Tx(TEXT("weather.rain")), Tx(TEXT("weather.fog")), Tx(TEXT("weather.snow")) },
		{ ESmvIcon::Sun, ESmvIcon::Cloud, ESmvIcon::Rain, ESmvIcon::Fog, ESmvIcon::Snow }, [Self]() { Self->Refresh(); }, 5))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.wind")), Slider(&T.WindMs, 0, 25, 0.5, Ms))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.wind_dir")), Slider(&T.WindDir, 0, 355, 5, Deg))];
	V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.gust")), Slider(&T.WindGust, 0, 15, 0.5, Ms))];
	V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SmvUi::Text(Tx(TEXT("parkur.weather_hint")), 10, "Regular", Mute())];
	return V;
}

TSharedRef<SWidget> SSmvShell::ParkurWizard()
{
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	PwStep = FMath::Clamp(PwStep, 0, 3);
	TabStep = [Self](int32 Dl) { Self->PwStep = FMath::Clamp(Self->PwStep + Dl, 0, 3); Self->Refresh(); };
	const TArray<FText> Steps = { Tx(TEXT("parkur.s_biome")), Tx(TEXT("parkur.s_world")), Tx(TEXT("parkur.s_weather")), Tx(TEXT("parkur.s_name")) };
	FSmvTrack& T = NewTrack;
	FSmvTrackWorld& W = T.World;
	const FString Bi = YhBiomeOf(T);
	auto Pct = [](double V) { return FString::Printf(TEXT("%.0f%%"), V * 100.0); };
	auto Mfmt = [](double V) { return FString::Printf(TEXT("%.0f m"), V); };
	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	if (PwStep == 0)
	{
		struct FB { const TCHAR* Id; const TCHAR* Title; const TCHAR* Desc; };
		static const FB Bs[] = { { TEXT("forest"), TEXT("biome.forest"), TEXT("biome.forest_d") }, { TEXT("city"), TEXT("biome.city"), TEXT("biome.city_d") }, { TEXT("mountain"), TEXT("biome.mountain"), TEXT("biome.mountain_d") } };
		TSharedRef<SHorizontalBox> Hb = SNew(SHorizontalBox);
		int32 i = 0;
		for (const FB& X : Bs)
		{
			const FString Id = X.Id;
			TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Hex(YhBiomeColor(Id))).MinWidth(330.f).IntroDelay(0.06f * i++).Selected(Bi == Id)
				.Text(Tx(X.Title)).SubText(Tx(X.Desc))
				.OnClick([Self, Id]() { const int32 Seed = Self->NewTrack.World.Seed; YhApplyBiome(Self->NewTrack, Id); Self->NewTrack.World.Seed = Seed; Self->PwStep = 1; Self->Refresh(); })
				.Extra()
				[
					SNew(SBox).WidthOverride(296.f).HeightOverride(170.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Hex(0x0b1222, 0.9f)).Padding(0.f)[YhBiomeArt(Id, 77u + (uint32)i, 296.f, 170.f, true)]]
						+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(10.f)
						[
							SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Hex(0x0b1222, 0.85f)).Padding(FMargin(7.f))[Icon(YhBiomeIcon(Id), 22.f, Hex(YhBiomeColor(Id)))]
						]
					]
				];
			if (Bi == Id) DefaultFocus = B;
			Hb->AddSlot().AutoWidth().Padding(0, 0, 14, 0)[B];
		}
		Body = SNew(SBox).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(0, 0, 0, 10))[Hb];
	}
	else
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		if (PwStep == 1)
		{
			V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(FSmvI18n::Format(TEXT("parkur.world_of"), { { TEXT("b"), FSmvI18n::T(FString(TEXT("biome.")) + Bi).ToString() } }), Hex(YhBiomeColor(Bi)))];
			if (Bi == TEXT("forest"))
			{
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.tree_density")), Slider(&W.TreeDensity, 0, 1, 0.05, Pct))];
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.water_density")), Slider(&W.Water, 0, 0.6, 0.02, Pct))];
			}
			else if (Bi == TEXT("city"))
			{
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.buildings")), SliderI(&W.BuildingCount, 10, 400, 5))];
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.vehicles")), SliderI(&W.Vehicles, 0, 200, 5))];
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.people")), SliderI(&W.People, 0, 300, 5))];
			}
			else
			{
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.mountains")), SliderI(&W.MountainCount, 1, 12))];
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.mountain_h")), Slider(&W.MountainHeight, 100, 1500, 10, Mfmt))];
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.water_density")), Slider(&W.Water, 0, 0.6, 0.02, Pct))];
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.tree_density")), Slider(&W.TreeDensity, 0, 1, 0.05, Pct))];
			}
			V->AddSlot().AutoHeight().Padding(0, 12, 0, 6)[Section(Tx(TEXT("parkur.layout")), Accent2())];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.size")), Slider(&W.SizeM, 1000, 8000, 250, Mfmt))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.runway")), Toggle(&W.bRunway))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.seed")), SliderI(&W.Seed, 1, 999999, 1))];
			V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Star).Text(Tx(TEXT("parkur.reroll"))).OnClick([Self]() { Self->NewTrack.World.Seed = FMath::RandRange(1, 999999); Self->Refresh(); })
			];
		}
		else if (PwStep == 2) V->AddSlot().AutoHeight()[EnvEditor(T)];
		else
		{
			V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("parkur.s_name")), Accent())];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.name")), TextEdit(&T.Name, false, FText::GetEmpty(), 280.f))];
			if (T.GroundMsl < -1000) T.GroundMsl = 0;
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.sea_level")), Slider(&T.GroundMsl, 0, 3000, 10, Mfmt), Tx(TEXT("parkur.sea_level_hint")))];
			V->AddSlot().AutoHeight().Padding(0, 14, 0, 0)
			[
				SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Card()).Padding(FMargin(14.f, 10.f))
				[
					SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %.1f km · %s · %s"), *FSmvI18n::T(FString(TEXT("biome.")) + Bi).ToString(), W.SizeM / 1000.0,
						*FSmvI18n::T(FString(TEXT("time.")) + T.TimeOfDay).ToString(), *FSmvI18n::T(FString(TEXT("weather.")) + T.Weather).ToString())), 12, "Regular", Fg(), false)
				]
			];
		}
		// sağ: canlı 3B dünya önizlemesi (parametre değişince 0,6 sn sonra yeniden kurulur)
		TSharedRef<SWidget> Prev = YhBiomeArt(Bi, (uint32)W.Seed, 560.f, 350.f, true);
		if (Scene() && FSmvUiPrefs::Get().bPreview3d && Scene()->GetPreviewTarget())
		{
			PreviewBrush.SetResourceObject(Scene()->GetPreviewTarget());
			PreviewBrush.ImageSize = FVector2D(960, 600);
			PreviewBrush.SetUVRegion(FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, 1.f)));
			bPreviewVisible = true;
			Prev = SNew(SBox).WidthOverride(560.f).HeightOverride(350.f)[SNew(SImage).Image(&PreviewBrush)];
		}
		Body = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SBox).WidthOverride(560.f)[SNew(SScrollBox) + SScrollBox::Slot()[Glass(V, FMargin(20.f), 0.88f)]]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(GlassCard()).Padding(4.f)[Prev]]
				+ SVerticalBox::Slot().AutoHeight().Padding(6, 8, 0, 0)[SmvUi::Text(Tx(TEXT("parkur.preview_hint")), 10, "Regular", Dim(), false)]
			];
	}
	const bool bLast = PwStep >= 3;
	TSharedRef<SSmvButton> Next = SNew(SSmvButton).Kind(bLast ? ESmvBtn::Hero : ESmvBtn::Primary).MinWidth(220.f)
		.Icon(bLast ? ESmvIcon::Save : ESmvIcon::Next).Accent(bLast ? Hex(0x34d399) : Accent())
		.Text(bLast ? Tx(TEXT("common.save")) : Tx(TEXT("tut.next")))
		.OnClick([Self, F]()
		{
			if (Self->PwStep < 3) { Self->PwStep++; Self->Refresh(); return; }
			FSmvTrack& Tr = Self->NewTrack;
			Tr.Name = Tr.Name.TrimStartAndEnd().Left(40);
			if (Tr.Name.IsEmpty()) { Self->Toast(FSmvI18n::T(TEXT("hw.name_required")).ToString(), 4); return; }
			Tr.ParkurKind = TEXT("custom");
			Tr.WorldMode = TEXT("procedural");
			Tr.RadiusM = FMath::Clamp(Tr.World.SizeM * 0.45, 300.0, 4000.0);
			Self->Data()->SaveTrack(Tr, FSmvDone::CreateLambda([Self, F](bool bOk, const FString& Msg)
			{
				Self->Toast(bOk ? FSmvI18n::T(TEXT("parkur.saved")).ToString() : Msg, bOk ? 6 : 3);
				if (bOk && F.IsValid()) { F->ParkurCat = TEXT("custom"); F->SetScreen(ESmvScreen::ParkurList); }
			}));
		});
	if (!DefaultFocus.IsValid()) DefaultFocus = Next;
	TSharedRef<SWidget> Footer = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[SNew(SSmvButton).Icon(ESmvIcon::Prev).Text(Tx(TEXT("tut.prev"))).IsEnabled(PwStep > 0).OnClick([Self]() { Self->PwStep = FMath::Max(0, Self->PwStep - 1); Self->Refresh(); })]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth()[Next];
	return YhPage(T.Id.IsEmpty() ? Tx(TEXT("parkur.custom_new")) : FText::FromString(T.Name), Tx(TEXT("parkur.custom_wiz_sub")),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[YhSteps(Steps, PwStep, [Self](int32 I) { Self->PwStep = I; Self->Refresh(); })]
		+ SVerticalBox::Slot().FillHeight(1.f)[Body]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[Footer]);
}

TSharedRef<SWidget> SSmvShell::RealWizard()
{
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	RwStep = FMath::Clamp(RwStep, 0, 2);
	TabStep = [Self](int32 Dl) { Self->RwStep = FMath::Clamp(Self->RwStep + Dl, 0, 2); Self->Refresh(); };
	const TArray<FText> Steps = { Tx(TEXT("parkur.s_location")), Tx(TEXT("parkur.s_weather")), Tx(TEXT("parkur.s_name")) };
	FSmvTrack& T = NewTrack;
	T.ParkurKind = TEXT("real");
	if (T.WorldMode != TEXT("google3d")) T.WorldMode = TEXT("real");
	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	if (RwStep == 0)
	{
		TSharedRef<SWidget> Search = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 0, 8, 0)[TextEdit(&RealQuery, false, Tx(TEXT("map.search_ph")), 320.f)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Globe).Text(Tx(TEXT("map.search")))
				.OnClick([Self]()
				{
					TWeakPtr<SSmvShell> Wk = StaticCastSharedRef<SSmvShell>(Self->AsShared());
					SSmvTileMap::Search(Self->RealQuery, [Wk](bool bOk, double La, double Lo, const FString& Name)
					{
						TSharedPtr<SSmvShell> S = Wk.Pin();
						if (!S.IsValid()) return;
						if (!bOk) { S->Toast(FSmvI18n::T(TEXT("map.not_found")).ToString(), 4); return; }
						S->NewTrack.Lat = La; S->NewTrack.Lon = Lo;
						if (S->NewTrack.Name.IsEmpty() || S->NewTrack.Name == FSmvI18n::T(TEXT("parkur.real_new_name")).ToString()) S->NewTrack.Name = Name.Left(32);
						if (S->RealMap.IsValid()) S->RealMap->CenterOn(La, Lo, 15);
						S->Toast(Name.Left(90), 6);
					});
				})];
		TSharedRef<SVerticalBox> Side = SNew(SVerticalBox);
		Side->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("parkur.s_location")), Hex(0x38bdf8))];
		Side->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[SmvUi::Text(Tx(TEXT("parkur.map_hint")), 11, "Regular", Dim())];
		Side->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("parkur.map_size")), Slider(&T.RadiusM, 250, 5000, 50, [](double V) { return FString::Printf(TEXT("%.1f × %.1f km"), V * 2.0 / 1000.0, V * 2.0 / 1000.0); }))];
		Side->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.world")), Chips(&T.WorldMode, { TEXT("real"), TEXT("google3d") }, { Tx(TEXT("world.real")), Tx(TEXT("world.google3d")) }, { ESmvIcon::Globe, ESmvIcon::Globe }, nullptr, 1))];
		Side->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SmvUi::Text(TAttribute<FText>::CreateLambda([Self]() { return FText::FromString(FString::Printf(TEXT("%.5f, %.5f"), Self->NewTrack.Lat, Self->NewTrack.Lon)); }), 13, "Mono", Fg(), false)
		];
		Side->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[SmvUi::Text(Tx(TEXT("parkur.real_note")), 10, "Regular", Mute())];
		Body = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 18, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[SNew(SBox).WidthOverride(900.f)[Search]]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder).BorderImage(GlassCard()).Padding(4.f)
					[
						SAssignNew(RealMap, SSmvTileMap).Size(FVector2D(900.f, 520.f)).Lat(&T.Lat).Lon(&T.Lon).RadiusM(&T.RadiusM)
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)[Glass(Side, FMargin(20.f), 0.9f)];
	}
	else if (RwStep == 1)
	{
		Body = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(560.f)[Glass(EnvEditor(T), FMargin(20.f), 0.9f)]]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBorder).BorderImage(GlassCard()).Padding(4.f)[SatThumb(T.Lat, T.Lon, 520.f, 330.f, true)]];
	}
	else
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("parkur.s_name")), Accent())];
		V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("tracks.name")), TextEdit(&T.Name, false, FText::GetEmpty(), 300.f))];
		V->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
		[
			SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Card()).Padding(FMargin(14.f, 10.f))
			[
				SmvUi::Text(FText::FromString(FString::Printf(TEXT("%.5f, %.5f · %.1f km · %s · %s"), T.Lat, T.Lon, T.RadiusM * 2.0 / 1000.0,
					*FSmvI18n::T(FString(TEXT("time.")) + T.TimeOfDay).ToString(), *FSmvI18n::T(FString(TEXT("weather.")) + T.Weather).ToString())), 12, "Regular", Fg(), false)
			]
		];
		Body = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(560.f)[Glass(V, FMargin(20.f), 0.9f)]]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBorder).BorderImage(GlassCard()).Padding(4.f)[SatThumb(T.Lat, T.Lon, 520.f, 330.f, true)]];
	}
	const bool bLast = RwStep >= 2;
	TSharedRef<SSmvButton> Next = SNew(SSmvButton).Kind(bLast ? ESmvBtn::Hero : ESmvBtn::Primary).MinWidth(220.f)
		.Icon(bLast ? ESmvIcon::Save : ESmvIcon::Next).Accent(bLast ? Hex(0x38bdf8) : Accent())
		.Text(bLast ? Tx(TEXT("common.save")) : Tx(TEXT("tut.next")))
		.OnClick([Self, F]()
		{
			if (Self->RwStep < 2) { Self->RwStep++; Self->Refresh(); return; }
			FSmvTrack& Tr = Self->NewTrack;
			Tr.Name = Tr.Name.TrimStartAndEnd().Left(40);
			if (Tr.Name.IsEmpty()) { Self->Toast(FSmvI18n::T(TEXT("hw.name_required")).ToString(), 4); return; }
			Tr.ParkurKind = TEXT("real");
			Self->Data()->SaveTrack(Tr, FSmvDone::CreateLambda([Self, F](bool bOk, const FString& Msg)
			{
				Self->Toast(bOk ? FSmvI18n::T(TEXT("parkur.saved")).ToString() : Msg, bOk ? 6 : 3);
				if (bOk && F.IsValid()) { F->ParkurCat = TEXT("real"); F->SetScreen(ESmvScreen::ParkurList); }
			}));
		});
	if (!DefaultFocus.IsValid()) DefaultFocus = Next;
	TSharedRef<SWidget> Footer = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[SNew(SSmvButton).Icon(ESmvIcon::Prev).Text(Tx(TEXT("tut.prev"))).IsEnabled(RwStep > 0).OnClick([Self]() { Self->RwStep = FMath::Max(0, Self->RwStep - 1); Self->Refresh(); })]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth()[Next];
	return YhPage(T.Id.IsEmpty() ? Tx(TEXT("parkur.real_new")) : FText::FromString(T.Name), Tx(TEXT("parkur.real_wiz_sub")),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[YhSteps(Steps, RwStep, [Self](int32 I) { Self->RwStep = I; Self->Refresh(); })]
		+ SVerticalBox::Slot().FillHeight(1.f)[Body]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[Footer]);
}

// ---------------------------------------------------------------------------
// MÜRETTEBAT: üstte pilot değiştir, ortada gerçek sahnede seçili pilot (MetaHuman / MCC), sağda seviye · XP · madalyalar;
// "+ Yeni pilot" sihirbazı (cinsiyet → vücut → yüz → aksesuar → ad soyad)
// ---------------------------------------------------------------------------

namespace
{
	/** Madalya rozeti: altıgen yerine yuvarlak plaka + yıldız; kazanılmamışsa soluk ve kilitli */
	TSharedRef<SWidget> YhMedal(const SmvCore::FMedal& M, bool bHas, float Sz)
	{
		const FLinearColor C = bHas ? YhMedalColor(M.Level) : FLinearColor(1, 1, 1, 0.18f);
		TSharedRef<SSmvCanvas> Cv = SNew(SSmvCanvas).Size(FVector2D(Sz, Sz)).Visibility(EVisibility::HitTestInvisible);
		Cv->Painter = [C, bHas, Sz](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			const FVector2D Ct(Sz * 0.5f, Sz * 0.5f);
			if (bHas) DrGlow(Out, L, G, Ct, Sz * 0.62f, C.CopyWithNewOpacity(0.25f + 0.1f * FMath::Sin((float)FPlatformTime::Seconds() * 2.f)));
			DrBox(Out, L + 1, G, Ct - FVector2D(Sz * 0.42f, Sz * 0.42f), FVector2D(Sz * 0.84f, Sz * 0.84f), bHas ? Hex(0x0b1222, 0.9f) : Hex(0x0b1222, 0.5f), Circle());
			DrArc(Out, L + 2, G, Ct, Sz * 0.42f, 0.f, 360.f, C, bHas ? 2.5f : 1.2f);
			DrArc(Out, L + 2, G, Ct, Sz * 0.33f, 0.f, 360.f, C.CopyWithNewOpacity(C.A * 0.45f), 1.f);
			DrIcon(Out, L + 3, G, bHas ? ESmvIcon::Star : ESmvIcon::Shield, Ct, Sz * 0.36f, C, 2.f);
		};
		return Cv;
	}
}

TSharedRef<SWidget> SSmvShell::CrewV2()
{
	USmvDataService* D = Data();
	if (!D || !Flow.IsValid()) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	const FSmvPilot* Act = D->DefaultPilot();
	if (!Act && D->Pilots().Num()) Act = &D->Pilots()[0];
	// üst şerit: pilot değiştir (seçili = etkin pilot; tıklayınca etkin yapılır) + "+ Yeni pilot"
	TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
	Strip->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[SmvUi::Text(Tx(TEXT("crew2.switch")), 11, "Bold", Dim(), false)];
	for (const FSmvPilot& X : D->Pilots())
	{
		const FString Pid = X.Id;
		const bool bAct = Act && Act->Id == X.Id;
		const int32 Lv = SmvCore::LevelForXp(X.Xp);
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Normal).Selected(bAct).Accent(YhHex(X.Avatar.Helmet, Accent()))
			.OnClick([F, Pid]() { if (F.IsValid()) F->SwitchPilot(Pid); })
			.Extra()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SBox).WidthOverride(30.f).HeightOverride(33.f)[Portrait(&X.Avatar, 30.f)]]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(X.Name), 12, "Bold", Fg(), false)]
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FSmvI18n::Format(TEXT("level.short"), { { TEXT("n"), FString::FromInt(Lv) } }), 10, "Regular", bAct ? Accent() : Dim(), false)]
				]
			];
		if (bAct) DefaultFocus = B;
		Strip->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[B];
	}
	Strip->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
	[
		SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Plus).Text(Tx(TEXT("crew.new")))
		.OnClick([Self, F]()
		{
			FSmvPilot P;
			P.Avatar = FSmvAvatar();
			Self->NewPilot = P;
			Self->CwStep = 0; Self->CwFirst.Empty(); Self->CwLast.Empty();
			if (F.IsValid()) F->SetScreen(ESmvScreen::CrewWizard);
		})
	];
	if (!Act)
		return YhPage(Tx(TEXT("menu.crew")), Tx(TEXT("crew2.sub")), SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Strip]);
	// sağ kart: seviye, XP çubuğu, madalyalar, istatistik
	const FSmvPilot& P = *Act;
	const int32 Lv = SmvCore::LevelForXp(P.Xp);
	const long long Nx = SmvCore::XpForLevel(FMath::Min(Lv + 1, 100));
	const bool bMax = Lv >= 100;
	TSharedRef<SVerticalBox> Info = SNew(SVerticalBox);
	Info->AddSlot().AutoHeight()[Title(FText::FromString(P.Name), 26)];
	Info->AddSlot().AutoHeight().Padding(0, 2, 0, 14)[SmvUi::Text(FSmvI18n::Format(TEXT("crew.stats"), { { TEXT("n"), FString::FromInt(P.Flights) }, { TEXT("h"), FString::Printf(TEXT("%.1f"), P.Hours) } }), 12, "Regular", Dim(), false)];
	Info->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)
		[
			SNew(SBox).WidthOverride(84.f).HeightOverride(84.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(Accent().CopyWithNewOpacity(0.14f))]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					// rütbe simgesi (Space Rank paketi) varsa simge + küçük seviye rakamı, yoksa yalnız rakam
					YhRankBrush(Lv, 60.f)
					? YhW<SVerticalBox>(SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SImage).Image(YhRankBrush(Lv, 60.f))]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SmvUi::Text(FSmvI18n::Format(TEXT("level.short"), { { TEXT("n"), FString::FromInt(Lv) } }), 10, "Bold", Accent(), false)])
					: YhW<SVerticalBox>(SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SmvUi::Text(Tx(TEXT("crew2.level")), 9, "Bold", Dim(), false)]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Title(FText::AsNumber(Lv), 32, Accent())])
				]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%lld XP"), (long long)P.Xp)), 16, "Bold", Fg(), false)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 6)[XpBar(P.Xp, 230.f, 8.f)]
			+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(bMax ? Tx(TEXT("crew2.max")) : FSmvI18n::Format(TEXT("crew2.to_next"), { { TEXT("xp"), FString::Printf(TEXT("%lld"), FMath::Max(0LL, Nx - (long long)P.Xp)) },
				{ TEXT("n"), FString::FromInt(Lv + 1) } }), 10, "Regular", Mute(), false)]
		]
	];
	if (P.XpUnverified > 0)
		Info->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SmvUi::Text(FSmvI18n::Format(TEXT("crew2.unverified"), { { TEXT("xp"), FString::Printf(TEXT("%lld"), (long long)P.XpUnverified) } }), 10, "Regular", Warn())];
	Info->AddSlot().AutoHeight().Padding(0, 18, 0, 8)[Section(Tx(TEXT("crew2.medals")), Hex(0xfbbf24))];
	TSharedRef<SHorizontalBox> Med = SNew(SHorizontalBox);
	for (const SmvCore::FMedal& M : SmvCore::Medals())
	{
		const bool bHas = Lv >= M.Level;
		Med->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[YhMedal(M, bHas, 50.f)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 3, 0, 0)[SmvUi::Text(FSmvI18n::T(FString(TEXT("medal.")) + UTF8_TO_TCHAR(M.Id)), 9, "Bold", bHas ? Fg() : Mute(), false)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SmvUi::Text(FSmvI18n::Format(TEXT("level.short"), { { TEXT("n"), FString::FromInt(M.Level) } }), 9, "Regular", Mute(), false)]
		];
	}
	Info->AddSlot().AutoHeight()[Med];
	// son XP kayıtları (yerel defter; hesapta sunucu olayları seviye ucundan)
	if (P.XpLog.Num())
	{
		Info->AddSlot().AutoHeight().Padding(0, 16, 0, 6)[Section(Tx(TEXT("crew2.recent")), Accent3())];
		for (int32 i = P.XpLog.Num() - 1, n = 0; i >= 0 && n < 4; --i, ++n)
		{
			const FSmvXpEntry& E = P.XpLog[i];
			Info->AddSlot().AutoHeight().Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Icon(YhModeIcon(E.Mode), 14.f, Hex(YhModeColor(E.Mode)))]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *FSmvI18n::T(FString(TEXT("gmode.")) + (E.Mode.IsEmpty() ? FString(TEXT("free")) : E.Mode)).ToString(), *E.At.Left(10))), 11, "Regular", Dim(), false)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(FString::Printf(TEXT("+%d XP"), E.Xp)), 11, "Bold", Accent3(), false)]
			];
		}
	}
	const FSmvPilot Copy = P;
	Info->AddSlot().AutoHeight().Padding(0, 18, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Pilot).Text(Tx(TEXT("pilotmenu.customize")))
			.OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Customize); })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[SNew(SSmvButton).Icon(ESmvIcon::Replay).Text(Tx(TEXT("pilotmenu.flights")))
			.OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Logbook); })]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Trash).IsEnabled(D->Pilots().Num() > 1)
			.OnClick([Self, Copy]()
			{
				Self->Confirm(FSmvI18n::T(TEXT("dlg.delete_title")), FSmvI18n::Format(TEXT("dlg.delete_body"), { { TEXT("n"), Copy.Name } }), FSmvI18n::T(TEXT("dlg.yes_delete")), true, [Self, Copy]()
				{
					Self->Data()->DeletePilot(Copy.Id, FSmvDone::CreateLambda([Self](bool bOk, const FString& Msg)
					{
						Self->Toast(bOk ? FSmvI18n::T(TEXT("common.deleted")).ToString() : Msg, bOk ? 6 : 3);
						if (bOk && Self->Data()->Pilots().Num() && Self->Flow.IsValid()) Self->Flow->SwitchPilot(Self->Data()->Pilots()[0].Id);
						Self->Refresh();
					}));
				});
			})]
	];
	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarVisibility(EVisibility::Collapsed) + SScrollBox::Slot()[Strip]]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(0, 0, 0, 18)
			[
				SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SmvUi::Text(Tx(TEXT("crew2.active")), 11, "Bold", Accent(), false)]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Title(FText::FromString(P.Name), 34)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SBox).WidthOverride(430.f)[SNew(SScrollBox) + SScrollBox::Slot()[Glass(Info, FMargin(22.f), 0.9f)]]
			]
		];
	return YhPage(Tx(TEXT("menu.crew")), Tx(TEXT("crew2.sub")), Body);
}

TSharedRef<SWidget> SSmvShell::CrewWizard()
{
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	CwStep = FMath::Clamp(CwStep, 0, 4);
	TabStep = [Self](int32 Dl) { Self->CwStep = FMath::Clamp(Self->CwStep + Dl, 0, 4); Self->Refresh(); };
	const TArray<FText> Steps = { Tx(TEXT("cw.s_gender")), Tx(TEXT("cw.s_body")), Tx(TEXT("cw.s_face")), Tx(TEXT("cw.s_acc")), Tx(TEXT("cw.s_name")) };
	FSmvAvatar& A = NewPilot.Avatar;
	auto Re = [Self]() { Self->Refresh(); };
	// seçim kartları (simge + başlık + açıklama); seçim sonraki adıma geçer
	auto Cards = [this, Self](FString* Target, const TArray<FString>& Vals, const TArray<FText>& Labels, const TArray<FText>& Descs, const TArray<ESmvIcon>& Ics, bool bAdvance) -> TSharedRef<SWidget>
	{
		TSharedRef<SHorizontalBox> Hb = SNew(SHorizontalBox);
		for (int32 i = 0; i < Vals.Num(); ++i)
		{
			const FString V = Vals[i];
			const bool bSel = *Target == V;
			TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Icon(Ics.IsValidIndex(i) ? Ics[i] : ESmvIcon::Pilot).MinWidth(250.f).MinHeight(170.f).Selected(bSel).IntroDelay(0.05f * i)
				.Text(Labels.IsValidIndex(i) ? Labels[i] : FText::FromString(V)).SubText(Descs.IsValidIndex(i) ? Descs[i] : FText::GetEmpty())
				.OnClick([Self, Target, V, bAdvance]()
				{
					*Target = V;
					FSmvAvatar& Av = Self->NewPilot.Avatar;
					// cinsiyete uygun varsayılanlar (saç / sakal)
					if (Target == &Av.Body)
					{
						if (V == TEXT("f")) { Av.Beard = TEXT("none"); if (Av.Hair == TEXT("short") || Av.Hair == TEXT("buzz") || Av.Hair == TEXT("bald")) Av.Hair = TEXT("ponytail"); }
						else if (Av.Hair == TEXT("ponytail") || Av.Hair == TEXT("bun") || Av.Hair == TEXT("long")) Av.Hair = TEXT("short");
					}
					if (bAdvance) Self->CwStep = FMath::Min(Self->CwStep + 1, 4);
					Self->Refresh();
				});
			if (bSel) DefaultFocus = B;
			Hb->AddSlot().AutoWidth().Padding(0, 0, 12, 0)[B];
		}
		return Hb;
	};
	auto En = [](const TCHAR* Field, std::initializer_list<const TCHAR*> Vals, TArray<FString>& V, TArray<FText>& L)
	{
		for (const TCHAR* X : Vals) { V.Add(X); L.Add(FSmvI18n::T(FString::Printf(TEXT("avatar.%s_%s"), Field, X))); }
	};
	auto Pick = [this, Re, En](FString* Target, const TCHAR* Field, std::initializer_list<const TCHAR*> Vals, int32 PerRow) -> TSharedRef<SWidget>
	{
		TArray<FString> V; TArray<FText> L;
		En(Field, Vals, V, L);
		// sihirbaz paneli dar (≈ 460 px): etiket üstte, seçenekler altta tam genişlikte, satırda en çok 4 (taşıyordu)
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 4)[SmvUi::Text(FSmvI18n::T(FString(TEXT("avatar.")) + Field), 12, "Regular", Dim(), false)]
			+ SVerticalBox::Slot().AutoHeight()[Chips(Target, V, L, {}, Re, FMath::Min(PerRow, 4))];
	};
	auto RowV = [](const FText& Label, TSharedRef<SWidget> W) -> TSharedRef<SWidget>
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 4)[SmvUi::Text(Label, 12, "Regular", Dim(), false)]
			+ SVerticalBox::Slot().AutoHeight()[W];
	};
	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	if (CwStep == 0)
	{
		Body = SNew(SBox).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(0, 0, 0, 10))
		[
			Cards(&A.Body, { TEXT("m"), TEXT("f") }, { Tx(TEXT("cw.male")), Tx(TEXT("cw.female")) }, { Tx(TEXT("cw.male_d")), Tx(TEXT("cw.female_d")) }, { ESmvIcon::Pilot, ESmvIcon::User }, true)
		];
	}
	else if (CwStep == 1)
	{
		Body = SNew(SBox).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(0, 0, 0, 10))
		[
			Cards(&A.Build, { TEXT("slim"), TEXT("avg"), TEXT("broad") }, { Tx(TEXT("avatar.build_slim")), Tx(TEXT("avatar.build_avg")), Tx(TEXT("avatar.build_broad")) },
				{ Tx(TEXT("cw.slim_d")), Tx(TEXT("cw.avg_d")), Tx(TEXT("cw.broad_d")) }, { ESmvIcon::User, ESmvIcon::Pilot, ESmvIcon::Shield }, true)
		];
	}
	else
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		if (CwStep == 2)
		{
			V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("cw.s_face")), Accent())];
			{
				TSharedRef<SHorizontalBox> Sk = SNew(SHorizontalBox);
				static const TCHAR* Skins[8] = { TEXT("#f5d6c0"), TEXT("#eac3a2"), TEXT("#d9a77e"), TEXT("#c48a5c"), TEXT("#a8714a"), TEXT("#8a5a3a"), TEXT("#6b432b"), TEXT("#4a2e1e") };
				for (int32 k = 0; k < 8; ++k)
				{
					const FString Sv = FString::Printf(TEXT("s%d"), k + 1);
					Sk->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
					[
						SNew(SSmvButton).Kind(ESmvBtn::Ghost).MinHeight(36.f).Selected(A.Skin == Sv)
						.OnClick([Self, Sv]() { Self->NewPilot.Avatar.Skin = Sv; Self->Refresh(); })
						.Extra()[SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)[SNew(SBorder).BorderImage(Circle()).BorderBackgroundColor(YhHex(Skins[k]))]]
					];
				}
				V->AddSlot().AutoHeight().Padding(0, 3)[RowV(Tx(TEXT("avatar.skin")), Sk)];
			}
			V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Face, TEXT("face"), { TEXT("oval"), TEXT("round"), TEXT("square"), TEXT("long"), TEXT("heart") }, 5)];
			V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Eyes, TEXT("eyes"), { TEXT("almond"), TEXT("round"), TEXT("narrow"), TEXT("wide") }, 4)];
			V->AddSlot().AutoHeight().Padding(0, 3)[RowV(Tx(TEXT("avatar.eye_color")), ColorPick(&A.EyeColor, Re))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Brows, TEXT("brows"), { TEXT("straight"), TEXT("arched"), TEXT("angled"), TEXT("soft") }, 4)];
			V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Nose, TEXT("nose"), { TEXT("small"), TEXT("straight"), TEXT("wide"), TEXT("pointed") }, 4)];
			V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Mouth, TEXT("mouth"), { TEXT("neutral"), TEXT("smile"), TEXT("grin"), TEXT("serious") }, 4)];
			if (A.Body != TEXT("f")) V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Beard, TEXT("beard"), { TEXT("none"), TEXT("stubble"), TEXT("mustache"), TEXT("goatee"), TEXT("full") }, 5)];
			V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Hair, TEXT("hair"), { TEXT("short"), TEXT("buzz"), TEXT("spiky"), TEXT("sidepart"), TEXT("curly"), TEXT("mohawk"), TEXT("bald"), TEXT("long"), TEXT("ponytail"), TEXT("bun"), TEXT("bob") }, 4)];
			V->AddSlot().AutoHeight().Padding(0, 3)[RowV(Tx(TEXT("avatar.hair_color")), ColorPick(&A.HairColor, Re))];
		}
		else if (CwStep == 3)
		{
			V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("cw.s_acc")), Accent2())];
			V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Headgear, TEXT("headgear"), { TEXT("helmet"), TEXT("none"), TEXT("cap"), TEXT("headset") }, 4)];
			V->AddSlot().AutoHeight().Padding(0, 3)[RowV(Tx(TEXT("avatar.gear_color")), ColorPick(&A.Helmet, Re))];
			if (A.Headgear == TEXT("helmet") || A.Headgear.IsEmpty())
			{
				V->AddSlot().AutoHeight().Padding(0, 3)[RowV(Tx(TEXT("avatar.visor")), ColorPick(&A.Visor, Re))];
				V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.mirror")), Toggle(&A.bMirror, Re))];
			}
			V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Glasses, TEXT("glasses"), { TEXT("none"), TEXT("aviator"), TEXT("sun") }, 3)];
			V->AddSlot().AutoHeight().Padding(0, 3)[Pick(&A.Outfit, TEXT("outfit"), { TEXT("suit"), TEXT("jacket"), TEXT("casual") }, 3)];
			V->AddSlot().AutoHeight().Padding(0, 3)[RowV(Tx(TEXT("avatar.suit")), ColorPick(&A.Suit, Re))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.accessory")), Cycle(&A.Accessory, { TEXT("none"), TEXT("mic"), TEXT("crest"), TEXT("antenna"), TEXT("stripe") },
				{ Tx(TEXT("avatar.acc_none")), Tx(TEXT("avatar.acc_mic")), Tx(TEXT("avatar.acc_crest")), Tx(TEXT("avatar.acc_antenna")), Tx(TEXT("avatar.acc_stripe")) }, Re, 240.f))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("avatar.patch")), Cycle(&A.Patch, { TEXT("none"), TEXT("star"), TEXT("wing"), TEXT("bolt"), TEXT("chevron"), TEXT("target"), TEXT("shield"), TEXT("cross") },
				{ Tx(TEXT("avatar.patch_none")), Tx(TEXT("avatar.patch_star")), Tx(TEXT("avatar.patch_wing")), Tx(TEXT("avatar.patch_bolt")), Tx(TEXT("avatar.patch_chevron")), Tx(TEXT("avatar.patch_target")), Tx(TEXT("avatar.patch_shield")), Tx(TEXT("avatar.patch_cross")) }, Re, 240.f))];
		}
		else
		{
			V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("cw.s_name")), Accent3())];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("cw.first")), TextEdit(&CwFirst, false, FText::GetEmpty(), 260.f))];
			V->AddSlot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("cw.last")), TextEdit(&CwLast, false, FText::GetEmpty(), 260.f))];
			V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[SmvUi::Text(Tx(TEXT("cw.level_note")), 11, "Regular", Dim())];
		}
		Body = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SBox).WidthOverride(560.f)[SNew(SScrollBox) + SScrollBox::Slot()[Glass(V, FMargin(20.f), 0.88f)]]
			];
	}
	const bool bLast = CwStep >= 4;
	TSharedRef<SSmvButton> Next = SNew(SSmvButton).Kind(bLast ? ESmvBtn::Hero : ESmvBtn::Primary).MinWidth(220.f)
		.Icon(bLast ? ESmvIcon::Save : ESmvIcon::Next).Accent(bLast ? Hex(0x34d399) : Accent())
		.Text(bLast ? Tx(TEXT("cw.create")) : Tx(TEXT("tut.next")))
		.OnClick([Self, F]()
		{
			if (Self->CwStep < 4) { Self->CwStep++; Self->Refresh(); return; }
			const FString Nm = (Self->CwFirst.TrimStartAndEnd() + TEXT(" ") + Self->CwLast.TrimStartAndEnd()).TrimStartAndEnd().Left(40);
			if (Nm.IsEmpty()) { Self->Toast(FSmvI18n::T(TEXT("cw.name_required")).ToString(), 4); return; }
			FSmvPilot& P = Self->NewPilot;
			P.Name = Nm;
			P.Xp = 0; P.Level = 1;
			Self->Data()->SavePilot(P, FSmvDone::CreateLambda([Self, F, Nm](bool bOk, const FString& Msg)
			{
				if (!bOk) { Self->Toast(Msg, 3); return; }
				Self->Toast(FSmvI18n::Format(TEXT("crew.welcome"), { { TEXT("name"), Nm } }).ToString(), 6);
				// yeni pilot etkin pilot olur
				for (const FSmvPilot& X : Self->Data()->Pilots()) if (X.Name == Nm && F.IsValid()) { F->SwitchPilot(X.Id); }
				if (F.IsValid()) F->SetScreen(ESmvScreen::Crew);
			}));
		});
	if (!DefaultFocus.IsValid()) DefaultFocus = Next;
	TSharedRef<SWidget> Footer = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[SNew(SSmvButton).Icon(ESmvIcon::Prev).Text(Tx(TEXT("tut.prev"))).IsEnabled(CwStep > 0).OnClick([Self]() { Self->CwStep = FMath::Max(0, Self->CwStep - 1); Self->Refresh(); })]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(10, 0, 0, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Star).Text(Tx(TEXT("avatar.random")))
			.OnClick([Self]()
			{
				FSmvAvatar& Av = Self->NewPilot.Avatar;
				auto R = [](std::initializer_list<const TCHAR*> Ls) { const int32 Nn = (int32)Ls.size(); return FString(*(Ls.begin() + FMath::RandRange(0, Nn - 1))); };
				Av.Face = R({ TEXT("oval"), TEXT("round"), TEXT("square"), TEXT("long"), TEXT("heart") });
				Av.Eyes = R({ TEXT("almond"), TEXT("round"), TEXT("narrow"), TEXT("wide") });
				Av.Brows = R({ TEXT("straight"), TEXT("arched"), TEXT("angled"), TEXT("soft") });
				Av.Nose = R({ TEXT("small"), TEXT("straight"), TEXT("wide"), TEXT("pointed") });
				Av.Mouth = R({ TEXT("neutral"), TEXT("smile"), TEXT("grin"), TEXT("serious") });
				Av.Beard = Av.Body == TEXT("f") ? FString(TEXT("none")) : R({ TEXT("none"), TEXT("none"), TEXT("stubble"), TEXT("mustache"), TEXT("goatee"), TEXT("full") });
				Av.Hair = Av.Body == TEXT("f") ? R({ TEXT("long"), TEXT("ponytail"), TEXT("bun"), TEXT("bob"), TEXT("curly") }) : R({ TEXT("short"), TEXT("buzz"), TEXT("spiky"), TEXT("sidepart"), TEXT("curly") });
				Av.HairColor = R({ TEXT("#1c1410"), TEXT("#3b2a1e"), TEXT("#5a3b22"), TEXT("#8a5a2b"), TEXT("#c48a3f"), TEXT("#e1c27a") });
				Av.Skin = FString::Printf(TEXT("s%d"), FMath::RandRange(1, 8));
				Self->Refresh();
			})]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth()[Next];
	return YhPage(Tx(TEXT("cw.title")), Tx(TEXT("cw.sub")),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[YhSteps(Steps, CwStep, [Self](int32 I) { Self->CwStep = I; Self->Refresh(); })]
		+ SVerticalBox::Slot().FillHeight(1.f)[Body]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[Footer],
		[F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Crew); });
}

// ---------------------------------------------------------------------------
// UÇUŞ AKIŞI: dron → parkur (kategori: yalnız kayıtlı Özel/Gerçek varsa) → oyun türü → tek / çok oyunculu → UÇ
// Arka plan: seçilen dron kalkış alanında gerçek sahnede (alçak kahraman açısı)
// ---------------------------------------------------------------------------

namespace
{
	int32 YhFlyStepIx(int32 St) { static const int32 M[6] = { 0, 1, 1, 2, 3, 4 }; return M[FMath::Clamp(St, 0, 5)]; }

	bool YhHasAddon(const FSmvEquipment& E, const FString& Addon)
	{
		if (Addon == TEXT("sprayer")) return E.bSprayer;
		if (Addon == TEXT("gripper")) return E.bGripper;
		if (Addon == TEXT("parachute")) return E.bParachute;
		return true;
	}

	bool YhValidEmail(const FString& E)
	{
		int32 At = -1;
		if (!E.FindChar(TEXT('@'), At) || At < 1 || E.Len() > 120 || E.Contains(TEXT(" "))) return false;
		int32 Dot = -1;
		return E.FindLastChar(TEXT('.'), Dot) && Dot > At + 1 && Dot < E.Len() - 1;
	}
}

TSharedRef<SWidget> SSmvShell::FlyFlow()
{
	USmvDataService* D = Data();
	if (!D || !Flow.IsValid()) return SNullWidget::NullWidget;
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	int32 NCustom = 0, NReal = 0;
	for (const FSmvTrack& Tr : D->Tracks()) { if (Tr.IsReal()) ++NReal; else if (Tr.IsCustom()) ++NCustom; }
	const bool bNoOwn = NCustom + NReal == 0;
	Flow->FlyStep = FMath::Clamp(Flow->FlyStep, 0, 5);
	// yol haritası: Özel / Gerçek parkur yoksa kategori adımı gelmez, doğrudan Karşılaşma parkurları
	if (Flow->FlyStep == 1 && bNoOwn) { Flow->FlyStep = FlyPrevStep > 1 ? 0 : 2; Flow->ParkurCat = TEXT("encounter"); }
	if (Flow->FlyStep >= 2 && Flow->Draft.Drone.Id.IsEmpty()) Flow->FlyStep = 0;
	FlyPrevStep = Flow->FlyStep;
	const int32 St = Flow->FlyStep;
	TabStep = [Self, F](int32 Dl) { if (!F.IsValid()) return; F->FlyStep = FMath::Clamp(F->FlyStep + Dl, 0, 5); Self->Refresh(); };
	const TArray<FText> Steps = { Tx(TEXT("fly2.s_drone")), Tx(TEXT("fly2.s_track")), Tx(TEXT("fly2.s_mode")), Tx(TEXT("fly2.s_players")), Tx(TEXT("fly2.s_go")) };
	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	switch (St)
	{
	case 0: Body = FlyDrones(); break;
	case 1: Body = FlyCats(); break;
	case 2: Body = FlyTracks(); break;
	case 3: Body = FlyModes(); break;
	case 4: Body = FlyPlayers(); break;
	default: Body = FlySummary(); break;
	}
	const bool bCanNext = (St == 0 && !Flow->Draft.Drone.Id.IsEmpty()) || (St == 1) || (St == 2 && !Flow->Draft.Track.Id.IsEmpty()) || St == 3 || St == 4;
	TSharedRef<SSmvButton> Next = SNew(SSmvButton).Kind(ESmvBtn::Primary).MinWidth(200.f).Icon(ESmvIcon::Next).Text(Tx(TEXT("tut.next"))).IsEnabled(bCanNext)
		.OnClick([Self, F]() { if (!F.IsValid()) return; F->FlyStep = FMath::Min(F->FlyStep + 1, 5); Self->Refresh(); });
	if (!DefaultFocus.IsValid() && St < 5) DefaultFocus = Next;
	// alt şerit: seçilenler özeti
	const FSmvFlightRequest& Dr = Flow->Draft;
	TArray<FString> Bits;
	if (!Dr.Drone.Id.IsEmpty()) Bits.Add(Dr.Drone.Name);
	if (St >= 3 && !Dr.Track.Id.IsEmpty()) Bits.Add(Dr.Track.Name);
	if (St >= 4) Bits.Add(FSmvI18n::T(FString(TEXT("gmode.")) + Dr.GameMode).ToString());
	TSharedRef<SWidget> Footer = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SSmvButton).Icon(ESmvIcon::Prev).Text(Tx(TEXT("tut.prev"))).IsEnabled(St > 0)
			.OnClick([Self, F]() { if (!F.IsValid()) return; F->FlyStep = FMath::Max(0, F->FlyStep - 1); Self->Refresh(); })]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).HAlign(HAlign_Center)[SmvUi::Text(FText::FromString(FString::Join(Bits, TEXT("   ›   "))), 12, "Bold", FLinearColor(1, 1, 1, 0.8f), false)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[St < 5 ? YhW<SSmvButton>(Next) : SNullWidget::NullWidget];
	const int32 Ix = YhFlyStepIx(St);
	return YhPage(Tx(TEXT("menu2.fly")), Tx(TEXT("fly2.sub")),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[YhSteps(Steps, Ix, [Self, F, bNoOwn](int32 I)
		{
			if (!F.IsValid()) return;
			static const int32 To[5] = { 0, 1, 3, 4, 5 };
			F->FlyStep = (I == 1 && bNoOwn) ? 2 : To[FMath::Clamp(I, 0, 4)];
			Self->Refresh();
		})]
		+ SVerticalBox::Slot().FillHeight(1.f)[Body]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[Footer]);
}

TSharedRef<SWidget> SSmvShell::FlyDrones()
{
	USmvDataService* D = Data();
	SSmvShell* Self = this;
	TWeakObjectPtr<USmvFlow> F = Flow;
	TSharedRef<SHorizontalBox> Hb = SNew(SHorizontalBox);
	int32 i = 0;
	for (const FSmvDrone& X : D->Drones())
	{
		const FSmvDrone Copy = X;
		const FSmvCatalogModel* M = USmvDataService::FindModel(X.ModelKey);
		const bool bSel = Flow->Draft.Drone.Id == X.Id;
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(YhHex(X.Color, Accent())).MinWidth(250.f).Selected(bSel).IntroDelay(0.05f * i++)
			.Text(FText::FromString(X.Name)).SubText(FText::FromString(FString::Printf(TEXT("%s · %s"), *FSmvI18n::T(FString(TEXT("type.")) + X.Type).ToString(), M ? *M->Label : *X.ModelKey)))
			.OnClick([Self, F, Copy]()
			{
				if (!F.IsValid()) return;
				F->Draft.Drone = Copy;
				F->Draft.Launch.Method = Copy.Type == TEXT("plane") ? FString(TEXT("wheels")) : F->Draft.Launch.Method;
				// türün araç tipiyle oynanamıyorsa serbest uçuşa düşer
				if (!USmvFlow::ModeAllowsType(F->Draft.GameMode, Copy.Type, F->Draft.Backend == ESmvFlightBackend::Cloud)) F->Draft.GameMode = TEXT("free");
				F->FlyStep = 1;
				Self->Refresh();
			})
			.Extra()[DroneThumb(X, 220.f, 130.f)];
		if (bSel || !DefaultFocus.IsValid()) DefaultFocus = B;
		Hb->AddSlot().AutoWidth().Padding(0, 0, 12, 0)[B];
	}
	Hb->AddSlot().AutoWidth().VAlign(VAlign_Bottom)
	[
		SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Plus).Text(Tx(TEXT("fly2.to_hangar"))).OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Hangar); })
	];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		+ SVerticalBox::Slot().AutoHeight()[SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[Hb]];
}

TSharedRef<SWidget> SSmvShell::FlyCats()
{
	USmvDataService* D = Data();
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	int32 NCustom = 0, NReal = 0;
	for (const FSmvTrack& Tr : D->Tracks()) { if (Tr.IsReal()) ++NReal; else if (Tr.IsCustom()) ++NCustom; }
	struct FC { const TCHAR* Cat; const TCHAR* Title; uint32 Col; ESmvIcon Ic; int32 N; };
	const FC Cs[3] = { { TEXT("encounter"), TEXT("parkur.enc"), 0xfbbf24, ESmvIcon::Match, FSmvFabContent::Maps().Num() },
		{ TEXT("custom"), TEXT("parkur.custom"), 0x34d399, ESmvIcon::Map, NCustom }, { TEXT("real"), TEXT("parkur.real"), 0x38bdf8, ESmvIcon::Globe, NReal } };
	TSharedRef<SHorizontalBox> Hb = SNew(SHorizontalBox);
	for (int32 i = 0; i < 3; ++i)
	{
		const FC& C = Cs[i];
		if (i > 0 && C.N == 0) continue;   // yalnız daha önce oluşturulmuş kategoriler
		const FString Cat = C.Cat;
		TSharedRef<SWidget> Pic = i == 0 ? YhBiomeArt(TEXT("mountain"), 5u, 300.f, 170.f, true)
			: i == 1 ? YhBiomeArt(TEXT("forest"), 9u, 300.f, 170.f, true) : SatThumb(41.0082, 28.9784, 300.f, 170.f, true);
		if (i == 0)
		{
			TArray<const FSlateBrush*> Shots;
			for (const FSmvFabMap& M : FSmvFabContent::Maps()) if (const FSlateBrush* Bb = YhMapPreview(M)) Shots.Add(Bb);
			if (Shots.Num()) Pic = YhSlides(Shots, 300.f, 170.f, 4.0);
		}
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Hex(C.Col)).MinWidth(330.f).Selected(Flow->ParkurCat == Cat).IntroDelay(0.06f * i)
			.Text(Tx(C.Title)).SubText(FSmvI18n::Format(TEXT("parkur.count"), { { TEXT("n"), FString::FromInt(C.N) } }))
			.OnClick([Self, F, Cat]() { if (!F.IsValid()) return; F->ParkurCat = Cat; F->FlyStep = 2; Self->Refresh(); })
			.Extra()[SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Hex(0x0b1222, 0.9f)).Padding(0.f)[Pic]];
		if (Flow->ParkurCat == Cat || !DefaultFocus.IsValid()) DefaultFocus = B;
		Hb->AddSlot().AutoWidth().Padding(0, 0, 14, 0)[B];
	}
	return SNew(SBox).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(0, 0, 0, 10))[Hb];
}

TSharedRef<SWidget> SSmvShell::FlyTracks()
{
	USmvDataService* D = Data();
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	const FString Cat = Flow->ParkurCat;
	TSharedRef<SHorizontalBox> Hb = SNew(SHorizontalBox);
	int32 N = 0;
	auto Add = [this, &Hb, &N](const FText& Nm, const FText& Sub, TSharedRef<SWidget> Pic, bool bSel, const FLinearColor& Col, TFunction<void()> Fn)
	{
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Col).MinWidth(300.f).Selected(bSel).IntroDelay(0.04f * N)
			.Text(Nm).SubText(Sub).OnClick(Fn)
			.Extra()[SNew(SBox).WidthOverride(268.f).HeightOverride(150.f)[SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Hex(0x0b1222, 0.9f)).Padding(0.f)[Pic]]];
		if (bSel || !DefaultFocus.IsValid()) DefaultFocus = B;
		Hb->AddSlot().AutoWidth().Padding(0, 0, 12, 0)[SNew(SBox).WidthOverride(300.f)[B]];
		++N;
	};
	if (Cat == TEXT("encounter"))
	{
		for (const FSmvFabMap& M : FSmvFabContent::Maps())
		{
			if (M.Kind != ESmvMapKind::Level && M.Kind != ESmvMapKind::ProcCity) continue;
			const FSmvTrack Tr = FSmvFabContent::MakeTrack(M);
			const FSlateBrush* Img = YhMapPreview(M);
			TSharedRef<SWidget> Pic = Img ? YhW<SImage>(SNew(SImage).Image(Img)) : YhBiomeArt(TEXT("forest"), GetTypeHash(M.Key), 268.f, 150.f, false);
			Add(M.Name, M.Description, Pic, Flow->Draft.Track.Id == Tr.Id, Hex(0xfbbf24), [Self, F, Tr]()
			{
				if (!F.IsValid()) return;
				F->Draft.Track = Tr;
				F->FlyStep = 3;
				Self->Refresh();
			});
		}
	}
	else
	{
		const bool bReal = Cat == TEXT("real");
		for (const FSmvTrack& X : D->Tracks())
		{
			if (bReal ? !X.IsReal() : !X.IsCustom()) continue;
			const FSmvTrack Tr = X;
			const FString Bi = YhBiomeOf(X);
			TSharedRef<SWidget> Pic = bReal ? SatThumb(X.Lat, X.Lon, 268.f, 150.f, false) : YhBiomeArt(Bi, (uint32)X.World.Seed, 268.f, 150.f, false);
			const FString Sub = bReal ? FString::Printf(TEXT("%.1f km · %s"), X.RadiusM * 2.0 / 1000.0, *FSmvI18n::T(FString(TEXT("weather.")) + X.Weather).ToString())
				: FString::Printf(TEXT("%s · %s"), *FSmvI18n::T(FString(TEXT("biome.")) + Bi).ToString(), *FSmvI18n::T(FString(TEXT("weather.")) + X.Weather).ToString());
			Add(FText::FromString(X.Name), FText::FromString(Sub), Pic, Flow->Draft.Track.Id == X.Id, bReal ? Hex(0x38bdf8) : Hex(YhBiomeColor(Bi)), [Self, F, Tr]()
			{
				if (!F.IsValid()) return;
				F->Draft.Track = Tr;
				F->FlyStep = 3;
				Self->Refresh();
			});
		}
	}
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().FillHeight(1.f);
	if (N == 0) V->AddSlot().AutoHeight()[Glass(SmvUi::Text(Tx(TEXT("parkur.enc_empty")), 13, "Regular", Dim()), FMargin(20.f), 0.9f)];
	V->AddSlot().AutoHeight()[SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll) + SScrollBox::Slot()[Hb]];
	// seçili parkurun saati / havası (bu uçuş için)
	if (!Flow->Draft.Track.Id.IsEmpty())
	{
		FSmvTrack& T = Flow->Draft.Track;
		V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
		[
			Glass(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[SmvUi::Text(FText::FromString(T.Name), 13, "Bold", Fg(), false)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)[Chips(&T.TimeOfDay, { TEXT("day"), TEXT("golden"), TEXT("dusk"), TEXT("night") },
					{ Tx(TEXT("time.day")), Tx(TEXT("time.golden")), Tx(TEXT("time.dusk")), Tx(TEXT("time.night")) }, { ESmvIcon::Sun, ESmvIcon::Sunset, ESmvIcon::Sunset, ESmvIcon::Moon }, nullptr, 4)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Chips(&T.Weather, { TEXT("clear"), TEXT("cloudy"), TEXT("rain"), TEXT("fog"), TEXT("snow") },
					{ Tx(TEXT("weather.clear")), Tx(TEXT("weather.cloudy")), Tx(TEXT("weather.rain")), Tx(TEXT("weather.fog")), Tx(TEXT("weather.snow")) },
					{ ESmvIcon::Sun, ESmvIcon::Cloud, ESmvIcon::Rain, ESmvIcon::Fog, ESmvIcon::Snow }, nullptr, 5)], FMargin(16.f, 10.f), 0.85f)
		];
	}
	return V;
}

TSharedRef<SWidget> SSmvShell::FlyModes()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	const FSmvFlightRequest& Dr = Flow->Draft;
	const bool bCloud = Dr.Backend == ESmvFlightBackend::Cloud;
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).PreferredSize(1400.f).UseAllottedSize(true);
	int32 i = 0;
	for (const FString& M : YhModes())
	{
		const bool bOk = USmvFlow::ModeAllowsType(M, Dr.Drone.Type, bCloud);
		const FString Need = USmvFlow::RequiredAddon(M);
		const bool bHasAddon = Need.IsEmpty() || YhHasAddon(Dr.Drone.Equipment, Need);
		const bool bSel = Dr.GameMode == M;
		FText Sub = YhT(FString(TEXT("gmode.")) + M + TEXT("_d"));
		if (!bOk) Sub = FSmvI18n::Format(TEXT("fly2.mode_type"), { { TEXT("t"), FSmvI18n::T(FString(TEXT("type.")) + Dr.Drone.Type).ToString() } });
		else if (!bHasAddon) Sub = FSmvI18n::Format(TEXT("fly2.mode_addon"), { { TEXT("a"), FSmvI18n::T(FString(TEXT("addon.")) + Need).ToString() } });
		const FString Mc = M;
		TSharedRef<SSmvButton> B = SNew(SSmvButton).Kind(ESmvBtn::Tile).Accent(Hex(YhModeColor(M))).Icon(YhModeIcon(M)).MinWidth(320.f).MinHeight(150.f).Selected(bSel).IntroDelay(0.04f * i++)
			.IsEnabled(bOk)
			.Text(YhT(FString(TEXT("gmode.")) + M)).SubText(Sub)
			.OnClick([Self, F, Mc]()
			{
				if (!F.IsValid()) return;
				F->Draft.GameMode = Mc;
				if (!USmvFlow::ModeIsMultiplayer(Mc)) F->bMulti = false;
				F->AiCount = Mc == TEXT("rings") ? FMath::Max(F->AiCount, 2) : Mc == TEXT("dogfight") ? 1 : 0;
				F->FlyStep = 4;
				Self->Refresh();
			});
		if (bSel || (!DefaultFocus.IsValid() && bOk)) DefaultFocus = B;
		Wrap->AddSlot().Padding(0, 0, 12, 12)[SNew(SBox).WidthOverride(340.f)[B]];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		+ SVerticalBox::Slot().AutoHeight()[SNew(SScrollBox) + SScrollBox::Slot()[Wrap]]
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 4, 0, 0)[SmvUi::Text(Tx(TEXT("fly2.rule")), 10, "Regular", Mute())];
}

TSharedRef<SWidget> SSmvShell::FlyPlayers()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	USmvFlow* Fl = Flow.Get();
	const FString Mode = Fl->Draft.GameMode;
	const bool bMp = USmvFlow::ModeIsMultiplayer(Mode);
	const bool bIn = Client() && Client()->IsLoggedIn();
	if (!bMp) Fl->bMulti = false;
	const int32 Auto = SmvCore::GameKindSingleAi(SmvCore::GameKindFromId(TCHAR_TO_UTF8(*Mode)));
	// iki büyük seçenek
	TSharedRef<SSmvButton> Solo = SNew(SSmvButton).Kind(ESmvBtn::Tile).Icon(ESmvIcon::Pilot).Accent(Hex(0xfbbf24)).MinWidth(360.f).MinHeight(150.f).Selected(!Fl->bMulti)
		.Text(Tx(TEXT("fly2.single"))).SubText(Auto > 0 || Mode == TEXT("rings") ? FSmvI18n::Format(TEXT("fly2.single_ai"), { { TEXT("n"), FString::FromInt(FMath::Max(Auto, Fl->AiCount)) } }) : Tx(TEXT("fly2.single_noai")))
		.OnClick([Self, F]() { if (F.IsValid()) { F->bMulti = false; Self->Refresh(); } });
	TSharedRef<SSmvButton> Multi = SNew(SSmvButton).Kind(ESmvBtn::Tile).Icon(ESmvIcon::Match).Accent(Hex(0x34d399)).MinWidth(360.f).MinHeight(150.f).Selected(Fl->bMulti).IsEnabled(bMp)
		.Text(Tx(TEXT("fly2.multi"))).SubText(bMp ? Tx(TEXT("fly2.multi_d")) : Tx(TEXT("fly2.multi_no")))
		.OnClick([Self, F]() { if (F.IsValid()) { F->bMulti = true; if (F->Friends.Num() == 0) F->FetchFriends(); Self->Refresh(); } });
	DefaultFocus = Fl->bMulti ? Multi : Solo;
	TSharedRef<SVerticalBox> Opt = SNew(SVerticalBox);
	auto AiRow = [this, Fl, Mode](int32 Lo, int32 Hi)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("fly2.ai_count")), SliderI(&Fl->AiCount, Lo, Hi, 1, [](double X) { return FString::FromInt((int32)X); }), Mode == TEXT("dogfight") ? Tx(TEXT("fly2.ai_dog")) : FText::GetEmpty())]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)[Row(Tx(TEXT("fly2.ai_level")), Chips(&Fl->AiLevel, { TEXT("easy"), TEXT("normal"), TEXT("hard") }, { Tx(TEXT("level.easy")), Tx(TEXT("level.normal")), Tx(TEXT("level.hard")) }, {}, nullptr, 3))];
	};
	if (!Fl->bMulti)
	{
		Opt->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("fly2.single")), Hex(0xfbbf24))];
		if (Mode == TEXT("rings")) { Fl->AiCount = FMath::Clamp(Fl->AiCount, 1, 7); Opt->AddSlot().AutoHeight()[AiRow(1, 7)]; }
		else if (Mode == TEXT("dogfight")) { Fl->AiCount = 1; Opt->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("fly2.ai_dog")), 12, "Regular", Dim())]; }
		else if (Mode == TEXT("facility_defense") || Mode == TEXT("ship_defense"))
			Opt->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("fly2.def_ai")), 12, "Regular", Dim())];
		else Opt->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("fly2.single_noai")), 12, "Regular", Dim())];
	}
	else if (!bIn)
	{
		Opt->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("fly2.multi")), Hex(0x34d399))];
		Opt->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[SmvUi::Text(Tx(TEXT("fly.multi_login")), 12, "Regular", Dim())];
		Opt->AddSlot().AutoHeight()[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Globe).Text(Tx(TEXT("welcome.login"))).OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Login); })];
	}
	else
	{
		Opt->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("fly2.invite")), Hex(0x34d399))];
		Opt->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)[TextEdit(&FlyEmail, false, Tx(TEXT("fly2.email_ph")), 260.f)]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Icon(ESmvIcon::Plus).Text(Tx(TEXT("fly2.add")))
				.OnClick([Self, F]()
				{
					if (!F.IsValid()) return;
					const FString E = Self->FlyEmail.TrimStartAndEnd().ToLower();
					if (!YhValidEmail(E)) { Self->Toast(FSmvI18n::T(TEXT("fly2.email_bad")).ToString(), 4); return; }
					if (F->InviteEmails.Num() >= 25) { Self->Toast(FSmvI18n::T(TEXT("fly2.email_max")).ToString(), 4); return; }
					F->InviteEmails.AddUnique(E);
					Self->FlyEmail.Empty();
					Self->Refresh();
				})]
		];
		TSharedRef<SWrapBox> Em = SNew(SWrapBox).PreferredSize(520.f).UseAllottedSize(true);
		for (const FString& E : Fl->InviteEmails)
		{
			const FString Ec = E;
			Em->AddSlot().Padding(0, 0, 6, 6)
			[
				SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Card()).Padding(FMargin(10.f, 2.f, 2.f, 2.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(E), 11, "Regular", Fg(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Trash).FontSize(10)
						.OnClick([Self, F, Ec]() { if (F.IsValid()) { F->InviteEmails.Remove(Ec); Self->Refresh(); } })]
				]
			];
		}
		Opt->AddSlot().AutoHeight()[Em];
		Opt->AddSlot().AutoHeight().Padding(0, 10, 0, 6)[Section(Tx(TEXT("pilotmenu.friends")), Accent())];
		if (Fl->bFriendsBusy) Opt->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("friends.loading")), 11, "Regular", Dim())];
		else if (Fl->Friends.Num() == 0) Opt->AddSlot().AutoHeight()[SmvUi::Text(Fl->FriendsError.IsEmpty() ? Tx(TEXT("friends.empty_short")) : FText::FromString(Fl->FriendsError), 11, "Regular", Mute())];
		for (const USmvFlow::FFriend& Fr : Fl->Friends)
		{
			const int64 Id = Fr.Id;
			const bool bOn = Fl->InviteFriends.Contains(Id);
			Opt->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(SSmvButton).Kind(ESmvBtn::Normal).LeftAlign(true).Selected(bOn).Accent(Hex(0x34d399))
				.OnClick([Self, F, Id]() { if (!F.IsValid()) return; if (F->InviteFriends.Contains(Id)) F->InviteFriends.Remove(Id); else F->InviteFriends.AddUnique(Id); Self->Refresh(); })
				.Extra()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Icon(bOn ? ESmvIcon::Check : ESmvIcon::User, 16.f, bOn ? Accent3() : Dim())]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(Fr.Name.IsEmpty() ? Fr.Email : Fr.Name), 12, "Bold", Fg(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FSmvI18n::Format(TEXT("friends.played"), { { TEXT("n"), FString::FromInt(Fr.Played) } }), 10, "Regular", Mute(), false)]
				]
			];
		}
		Opt->AddSlot().AutoHeight().Padding(0, 10, 0, 6)[Section(Tx(TEXT("fly2.ai")), Accent2())];
		if (Mode == TEXT("dogfight")) { Fl->AiCount = FMath::Clamp(Fl->AiCount, 0, 1); Opt->AddSlot().AutoHeight()[AiRow(0, 1)]; }
		else { Fl->AiCount = FMath::Clamp(Fl->AiCount, 0, 7); Opt->AddSlot().AutoHeight()[AiRow(0, 7)]; }
		Opt->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SmvUi::Text(Tx(TEXT("fly2.multi_note")), 10, "Regular", Mute())];
	}
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 18, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Solo]
			+ SVerticalBox::Slot().AutoHeight()[Multi]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(560.f)[SNew(SScrollBox) + SScrollBox::Slot()[Glass(Opt, FMargin(20.f), 0.9f)]]];
}

TSharedRef<SWidget> SSmvShell::FlySummary()
{
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	USmvFlow* Fl = Flow.Get();
	Fl->ApplyGameMode();
	const FSmvFlightRequest& Dr = Fl->Draft;
	const FString Err = Fl->ValidateDraft();
	const bool bIn = Client() && Client()->IsLoggedIn();
	const FString Mode = Dr.GameMode;
	FString Players;
	if (Fl->bMulti) Players = FSmvI18n::Format(TEXT("fly2.sum_multi"), { { TEXT("e"), FString::FromInt(Fl->InviteEmails.Num()) }, { TEXT("f"), FString::FromInt(Fl->InviteFriends.Num()) }, { TEXT("a"), FString::FromInt(Fl->AiCount) } }).ToString();
	else if (Fl->Race.Kind != TEXT("none")) Players = FSmvI18n::Format(TEXT("fly2.sum_ai"), { { TEXT("a"), FString::FromInt(Fl->Race.Bots) } }).ToString();
	else Players = FSmvI18n::T(TEXT("fly2.sum_solo")).ToString();
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Line = [&V](ESmvIcon I, const FLinearColor& C, const FText& K, const FString& Val)
	{
		V->AddSlot().AutoHeight().Padding(0, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Icon(I, 18.f, C)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[SNew(SBox).WidthOverride(120.f)[SmvUi::Text(K, 11, "Regular", Dim(), false)]]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(Val), 14, "Bold", Fg(), false)]
		];
	};
	const FSmvCatalogModel* Cm = USmvDataService::FindModel(Dr.Drone.ModelKey);
	Line(IconForType(Dr.Drone.Type), Accent(), Tx(TEXT("fly2.s_drone")), Dr.Drone.Name + (Cm ? FString(TEXT("  ·  ")) + Cm->Label : FString()));
	Line(ESmvIcon::Map, Accent3(), Tx(TEXT("fly2.s_track")), FString::Printf(TEXT("%s  ·  %s  ·  %s"), *Dr.Track.Name, *FSmvI18n::T(FString(TEXT("time.")) + Dr.Track.TimeOfDay).ToString(), *FSmvI18n::T(FString(TEXT("weather.")) + Dr.Track.Weather).ToString()));
	Line(YhModeIcon(Mode), Hex(YhModeColor(Mode)), Tx(TEXT("fly2.s_mode")), FSmvI18n::T(FString(TEXT("gmode.")) + Mode).ToString());
	Line(ESmvIcon::User, Hex(0x34d399), Tx(TEXT("fly2.s_players")), Players);
	Line(ESmvIcon::Camera, Dim(), Tx(TEXT("hw.camera")), FSmvI18n::T(FString(TEXT("cam2.")) + (Dr.Drone.Equipment.Camera.IsEmpty() ? FString(TEXT("day")) : Dr.Drone.Equipment.Camera)).ToString());
	if (bIn)
	{
		static FString BackendSel;
		BackendSel = Dr.Backend == ESmvFlightBackend::Cloud ? TEXT("cloud") : TEXT("local");
		V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Row(Tx(TEXT("fly2.backend")), Chips(&BackendSel, { TEXT("local"), TEXT("cloud") }, { Tx(TEXT("fly2.local")), Tx(TEXT("fly2.cloud")) }, { ESmvIcon::Monitor, ESmvIcon::Cloud },
			[Self, F]() { if (!F.IsValid()) return; F->Draft.Backend = BackendSel == TEXT("cloud") ? ESmvFlightBackend::Cloud : ESmvFlightBackend::LocalSitl; Self->Refresh(); }, 2))];
	}
	if (!Err.IsEmpty()) V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[SmvUi::Text(FText::FromString(Err), 12, "Bold", Danger())];
	TSharedRef<SSmvButton> Go = SNew(SSmvButton).Kind(ESmvBtn::Hero).Icon(ESmvIcon::Play).Accent(Hex(0xfbbf24)).MinWidth(420.f).IsEnabled(Err.IsEmpty())
		.Text(Tx(TEXT("fly2.go"))).SubText(Fl->bMulti ? Tx(TEXT("fly2.go_multi")) : Tx(TEXT("fly2.go_single")))
		.OnClick([F]() { if (F.IsValid()) F->StartGame(); });
	DefaultFocus = Go;
	V->AddSlot().AutoHeight().Padding(0, 16, 0, 0)[Go];
	V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
	[
		SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Gear).Text(Tx(TEXT("fly2.advanced")))
		.OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Wizard); })
	];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[SNew(SBox).WidthOverride(520.f)[Glass(V, FMargin(24.f), 0.9f)]];
}

// ---------------------------------------------------------------------------
// UÇUŞ SONRASI DENEYİM: XP sayacı, seviye halkası (seviye atlama), XP bileşenleri, manevralar, yeni madalyalar
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::ProgressScreen()
{
	if (!Flow.IsValid()) return SNullWidget::NullWidget;
	TWeakObjectPtr<USmvFlow> F = Flow;
	const USmvFlow::FXpResult& X = Flow->LastXp;
	const double At = ProgressAt > 0 ? ProgressAt : FPlatformTime::Seconds();
	const int64 Before = X.Before, After = FMath::Max(X.After, X.Before);
	const int32 Gain = X.Xp;
	const bool bPending = X.bPending;
	const double Dur = 2.4;
	auto Ease = [At, Dur]() { const double K = ReducedMotion() ? 1.0 : FMath::Clamp((FPlatformTime::Seconds() - At - 0.4) / Dur, 0.0, 1.0); return 1.0 - FMath::Pow(1.0 - K, 3.0); };
	// seviye halkası: XP Before → After akar; seviye geçişinde halka dolup sıfırlanır
	TSharedRef<SSmvCanvas> Ring = SNew(SSmvCanvas).Size(FVector2D(240, 240)).Visibility(EVisibility::HitTestInvisible);
	Ring->Painter = [Before, After, Ease, bPending](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D S = FVector2D(G.GetLocalSize());
		const FVector2D C = S * 0.5;
		const float R = (float)FMath::Min(S.X, S.Y) * 0.42f;
		const double K = bPending ? 0.0 : Ease();
		const long long Cur = (long long)FMath::Lerp((double)Before, (double)After, K);
		const int32 Lv = SmvCore::LevelForXp(Cur);
		const float P = (float)SmvCore::LevelProgress(Cur);
		const bool bUp = Lv > SmvCore::LevelForXp(Before);
		DrGlow(Out, L, G, C, R * 1.35f, (bUp ? Hex(0xfbbf24) : Accent()).CopyWithNewOpacity(0.18f + (bUp ? 0.12f * FMath::Sin((float)FPlatformTime::Seconds() * 5.f) : 0.f)));
		DrBox(Out, L + 1, G, C - FVector2D(R, R), FVector2D(R * 2.f, R * 2.f), Hex(0x0b1222, 0.85f), Circle());
		DrArc(Out, L + 2, G, C, R, 0.f, 360.f, FLinearColor(1, 1, 1, 0.12f), 10.f);
		if (P > 0.001f) DrArc(Out, L + 3, G, C, R, -90.f, -90.f + 360.f * P, bUp ? Hex(0xfbbf24) : Accent(), 10.f);
		// etiket / rakam / XP üst üste binmesin (rakam 56 pt ≈ 68 px yüksek): etiket üstte, XP altta
		DrText(Out, L + 4, G, SmvUi::SmvUpper(FSmvI18n::T(TEXT("crew2.level")).ToString()), C - FVector2D(0, R * 0.62f), Font(11, "Bold", 200), Dim(), 0);
		DrText(Out, L + 4, G, FString::FromInt(Lv), C + FVector2D(0, R * 0.0f), Font(56, "Black"), bUp ? Hex(0xfbbf24) : Fg(), 0);
		DrText(Out, L + 4, G, FString::Printf(TEXT("%lld XP"), Cur), C + FVector2D(0, R * 0.62f), Font(12, "Bold"), Dim(), 0);
	};
	const int32 Lv0 = X.LevelBefore, Lv1 = X.LevelAfter;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().HAlign(HAlign_Center)[SmvUi::Text(FSmvI18n::T(FString(TEXT("gmode.")) + (Flow->LastSummary.GameMode.IsEmpty() ? FString(TEXT("free")) : Flow->LastSummary.GameMode)), 12, "Bold", Accent(), false)];
	V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 6)[Title(Tx(TEXT("xp.title")), 30)];
	V->AddSlot().AutoHeight().HAlign(HAlign_Center)[Ring];
	V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)
	[
		SNew(STextBlock).Font(Font(40, "Black", 60)).ColorAndOpacity(Hex(0x34d399)).ShadowOffset(FVector2D(0, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f))
		.Text_Lambda([Gain, Ease, bPending]() { return bPending ? FSmvI18n::T(TEXT("xp.pending")) : FText::FromString(FString::Printf(TEXT("+%d XP"), (int32)FMath::RoundToDouble(Gain * Ease()))); })
	];
	if (Lv1 > Lv0)
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
		[
			SNew(SBorder).BorderImage(Round(14.f)).BorderBackgroundColor(Hex(0xfbbf24, 0.2f)).Padding(FMargin(18.f, 6.f))
			.Visibility_Lambda([Ease]() { return Ease() > 0.55 ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
			[
				SmvUi::Text(FSmvI18n::Format(TEXT("xp.level_up"), { { TEXT("n"), FString::FromInt(Lv1) } }), 16, "Black", Hex(0xfbbf24), false)
			]
		];
	if (!X.Note.IsEmpty()) V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[SmvUi::Text(FText::FromString(X.Note), 10, "Regular", Mute(), false)];
	// sağ: bileşenler + manevralar + madalyalar
	TSharedRef<SVerticalBox> Rt = SNew(SVerticalBox);
	Rt->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Section(Tx(TEXT("xp.parts")), Accent())];
	int32 Row0 = 0;
	for (const TPair<FString, int32>& Pp : X.Parts)
	{
		if (Pp.Value == 0) continue;
		const bool bMan = Pp.Key.StartsWith(TEXT("m:"));
		const FText Lb = bMan ? YhT(FString(TEXT("maneuver.")) + Pp.Key.Mid(2)) : YhT(FString(TEXT("xpp.")) + Pp.Key);
		const double Dl = 0.5 + 0.12 * Row0++;
		Rt->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Icon(bMan ? ESmvIcon::Rocket : ESmvIcon::Check, 14.f, bMan ? Accent2() : Accent3())]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(Lb, 12, "Regular", Fg(), false)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Accent3())
				.Text(FText::FromString(FString::Printf(TEXT("+%d"), Pp.Value)))
				.Visibility_Lambda([At, Dl]() { return ReducedMotion() || FPlatformTime::Seconds() - At > Dl ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
			]
		];
	}
	if (Row0 == 0) Rt->AddSlot().AutoHeight()[SmvUi::Text(bPending ? Tx(TEXT("xp.pending")) : Tx(TEXT("xp.none")), 11, "Regular", Mute())];
	if (X.Maneuvers.Num())
	{
		Rt->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(Tx(TEXT("xp.maneuvers")), Accent2())];
		TSharedRef<SWrapBox> Mw = SNew(SWrapBox).PreferredSize(420.f).UseAllottedSize(true);
		for (const TPair<FString, int32>& Mv : X.Maneuvers)
			Mw->AddSlot().Padding(0, 0, 6, 6)
			[
				SNew(SBorder).BorderImage(Round(10.f)).BorderBackgroundColor(Accent2().CopyWithNewOpacity(0.18f)).Padding(FMargin(10.f, 4.f))
				[
					SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s ×%d"), *YhT(FString(TEXT("maneuver.")) + Mv.Key).ToString(), Mv.Value)), 11, "Bold", Fg(), false)
				]
			];
		Rt->AddSlot().AutoHeight()[Mw];
	}
	if (X.NewMedals.Num())
	{
		Rt->AddSlot().AutoHeight().Padding(0, 14, 0, 6)[Section(Tx(TEXT("xp.new_medals")), Hex(0xfbbf24))];
		TSharedRef<SHorizontalBox> Mh = SNew(SHorizontalBox);
		for (const SmvCore::FMedal& M : SmvCore::Medals())
			if (X.NewMedals.Contains(FString(UTF8_TO_TCHAR(M.Id))))
				Mh->AddSlot().AutoWidth().Padding(0, 0, 10, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[YhMedal(M, true, 60.f)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SmvUi::Text(FSmvI18n::T(FString(TEXT("medal.")) + UTF8_TO_TCHAR(M.Id)), 10, "Bold", Fg(), false)]
				];
		Rt->AddSlot().AutoHeight()[Mh];
	}
	TSharedRef<SSmvButton> Sum = SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Book).MinWidth(200.f).Text(Tx(TEXT("xp.to_summary")))
		.OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Summary); });
	DefaultFocus = Sum;
	// düğmeler panel genişliğinde satır kırar (üç düğme 426 px'e sığmıyordu: "Ana menü" panelden taşıyordu)
	Rt->AddSlot().AutoHeight().Padding(0, 18, 0, 0)
	[
		SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 8.f))
		+ SWrapBox::Slot()[Sum]
		+ SWrapBox::Slot()[SNew(SSmvButton).Icon(ESmvIcon::Replay).Text(Tx(TEXT("xp.again")))
			.OnClick([F]() { if (F.IsValid()) { F->FlyStep = 5; F->SetScreen(ESmvScreen::FlyFlow); } })]
		+ SWrapBox::Slot()[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Home).Text(Tx(TEXT("xp.menu")))
			.OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Menu); })]
	];
	return SNew(SBox).Padding(FMargin(64.f, 40.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).HAlign(HAlign_Center)[Glass(V, FMargin(30.f, 24.f), 0.86f)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(470.f)[Glass(Rt, FMargin(22.f), 0.9f)]]
		];
}

// ---------------------------------------------------------------------------
// ARKADAŞLARIM: birlikte uçulan pilotlar (otomatik) + elle eklenenler; gelen davetler
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvShell::FriendsScreen()
{
	if (!Flow.IsValid()) return SNullWidget::NullWidget;
	TWeakObjectPtr<USmvFlow> F = Flow;
	SSmvShell* Self = this;
	USmvFlow* Fl = Flow.Get();
	const bool bIn = Client() && Client()->IsLoggedIn();
	if (!bIn)
	{
		TSharedRef<SSmvButton> Lg = SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Globe).Text(Tx(TEXT("welcome.login"))).OnClick([F]() { if (F.IsValid()) F->SetScreen(ESmvScreen::Login); });
		DefaultFocus = Lg;
		return YhPage(Tx(TEXT("pilotmenu.friends")), Tx(TEXT("friends.sub")),
			SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Top)
			[
				SNew(SBox).WidthOverride(560.f)
				[
					Glass(SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[Icon(ESmvIcon::User, 44.f, Accent())]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 6)[Title(Tx(TEXT("friends.empty_title")), 22)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)[SmvUi::Text(Tx(TEXT("friends.guest")), 12, "Regular", Dim())]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[Lg], FMargin(26.f), 0.9f)
				]
			]);
	}
	TSharedRef<SVerticalBox> L = SNew(SVerticalBox);
	L->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)[TextEdit(&FriendEmail, false, Tx(TEXT("fly2.email_ph")), 260.f)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Plus).Text(Tx(TEXT("friends.add")))
			.OnClick([Self, F]()
			{
				const FString E = Self->FriendEmail.TrimStartAndEnd();
				if (!YhValidEmail(E)) { Self->Toast(FSmvI18n::T(TEXT("fly2.email_bad")).ToString(), 4); return; }
				if (F.IsValid()) F->AddFriend(E);
				Self->FriendEmail.Empty();
				Self->Refresh();
			})]
	];
	if (Fl->bFriendsBusy) L->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("friends.loading")), 12, "Regular", Dim())];
	else if (!Fl->FriendsError.IsEmpty()) L->AddSlot().AutoHeight()[SmvUi::Text(FText::FromString(Fl->FriendsError), 12, "Regular", Warn())];
	else if (Fl->Friends.Num() == 0) L->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("friends.empty")), 12, "Regular", Mute())];
	for (const USmvFlow::FFriend& Fr : Fl->Friends)
	{
		const int64 Id = Fr.Id;
		const FString Em = Fr.Email;
		const bool bFriend = Fr.bFriend;
		L->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Card()).Padding(FMargin(12.f, 8.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[SNew(SBox).WidthOverride(40.f).HeightOverride(44.f)[Portrait(&Fr.Avatar, 40.f)]]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SmvUi::Text(FText::FromString(Fr.Name.IsEmpty() ? Fr.Email : Fr.Name), 13, "Bold", Fg(), false)]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
						[
							SNew(SBorder).BorderImage(Round(8.f)).BorderBackgroundColor((bFriend ? Accent3() : Accent()).CopyWithNewOpacity(0.2f)).Padding(FMargin(7.f, 1.f))
							[
								SmvUi::Text(bFriend ? Tx(TEXT("friends.badge_friend")) : Tx(TEXT("friends.badge_auto")), 9, "Bold", bFriend ? Accent3() : Accent(), false)
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight()[SmvUi::Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *Fr.Email,
						*FSmvI18n::Format(TEXT("friends.played_last"), { { TEXT("n"), FString::FromInt(Fr.Played) }, { TEXT("d"), Fr.Last.IsEmpty() ? FString(TEXT("—")) : Fr.Last } }).ToString())), 10, "Regular", Mute(), false)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					bFriend
					? YhW<SSmvButton>(SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Trash).FontSize(11).Text(Tx(TEXT("friends.remove"))).OnClick([F, Id]() { if (F.IsValid()) F->RemoveFriend(Id); }))
					: YhW<SSmvButton>(SNew(SSmvButton).Kind(ESmvBtn::Normal).Icon(ESmvIcon::Plus).FontSize(11).Text(Tx(TEXT("friends.add"))).OnClick([F, Em]() { if (F.IsValid()) F->AddFriend(Em); }))
				]
			]
		];
	}
	// gelen davetler
	TSharedRef<SVerticalBox> Rv = SNew(SVerticalBox);
	Rv->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Section(FSmvI18n::Format(TEXT("friends.invites"), { { TEXT("n"), FString::FromInt(Fl->Invites.Num()) } }), Hex(0xfbbf24))];
	if (Fl->Invites.Num() == 0) Rv->AddSlot().AutoHeight()[SmvUi::Text(Tx(TEXT("friends.no_invites")), 11, "Regular", Mute())];
	for (const TSharedPtr<FJsonObject>& Iv : Fl->Invites)
	{
		if (!Iv.IsValid()) continue;
		const int64 Iid = (int64)SmvJson::Num(Iv, TEXT("id"));
		const TSharedPtr<FJsonObject> Who = SmvJson::Obj(Iv, TEXT("inviter"));
		const FString By = Who.IsValid() ? SmvJson::Str(Who, TEXT("name"), SmvJson::Str(Who, TEXT("email"))) : FString(TEXT("?"));
		const FString Md = SmvJson::Str(Iv, TEXT("mode"), TEXT("rings"));
		const FString TrN = SmvJson::Str(Iv, TEXT("track"));
		Rv->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SBorder).BorderImage(Round(12.f)).BorderBackgroundColor(Card()).Padding(FMargin(12.f, 10.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Icon(YhModeIcon(Md), 16.f, Hex(YhModeColor(Md)))]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SmvUi::Text(FSmvI18n::Format(TEXT("friends.invite_from"), { { TEXT("n"), By }, { TEXT("m"), FSmvI18n::T(FString(TEXT("gmode.")) + Md).ToString() } }), 12, "Bold", Fg(), false)]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(24, 2, 0, 6)[SmvUi::Text(FText::FromString(TrN), 10, "Regular", Mute(), false)]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Check).FontSize(11).Text(Tx(TEXT("friends.accept"))).OnClick([F, Iid]() { if (F.IsValid()) F->AnswerInvite(Iid, true); })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SSmvButton).Kind(ESmvBtn::Ghost).Icon(ESmvIcon::Back).FontSize(11).Text(Tx(TEXT("friends.decline"))).OnClick([F, Iid]() { if (F.IsValid()) F->AnswerInvite(Iid, false); })]
				]
			]
		];
	}
	return YhPage(Tx(TEXT("pilotmenu.friends")), Tx(TEXT("friends.sub")),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 18, 0)[SNew(SBox).WidthOverride(640.f)[SNew(SScrollBox) + SScrollBox::Slot()[Glass(L, FMargin(20.f), 0.9f)]]]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[SNew(SBox).WidthOverride(400.f)[Glass(Rv, FMargin(20.f), 0.9f)]]);
}
