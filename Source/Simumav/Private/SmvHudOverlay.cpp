#include "SmvHudOverlay.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Fonts/SlateFontInfo.h"

void USmvHudOverlay::SetState(const FSmvHudState& S)
{
	State = S;
	if (S.Pip)
	{
		PipBrush.SetResourceObject(S.Pip);
		PipBrush.ImageSize = FVector2D(S.Pip->SizeX, S.Pip->SizeY);
		PipBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	BlinkT = FPlatformTime::Seconds();
}

namespace
{
	void SmvText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Pos, const FString& Text, int32 Size, const FLinearColor& C, bool bBold = true)
	{
		const FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
		FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2D(1200, Size * 1.6f), FSlateLayoutTransform(FVector2f(Pos))), Text, F, ESlateDrawEffect::None, C);
	}

	void SmvBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Pos, const FVector2D& Size, const FLinearColor& C, const FSlateBrush* Brush)
	{
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(FVector2f(Pos))), Brush, ESlateDrawEffect::None, C);
	}
}

int32 USmvHudOverlay::NativePaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	Layer = Super::NativePaint(Args, G, Cull, Out, Layer, Style, bParentEnabled);
	const FVector2D Sz = G.GetLocalSize();
	const FSlateBrush* W = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FLinearColor Panel(0.f, 0.f, 0.f, 0.45f), Txt(0.92f, 0.95f, 0.98f, 1.f), Dim(0.7f, 0.75f, 0.8f, 1.f);
	const FLinearColor Red(1.f, 0.15f, 0.12f, 1.f), Amber(1.f, 0.75f, 0.1f, 1.f), Green(0.2f, 1.f, 0.35f, 1.f);
	const bool bBlink = FMath::Fmod(FPlatformTime::Seconds(), 0.8) < 0.45;
	++Layer;

	// hesap / fizik / kip satırları, güç göstergeleri, ileti akışı ve RC çubukları artık SSmvFlightUi'da (bilgi kartı, kadranlar, sanal kumanda)
	// uyarılar (orta üst): kırmızı WARNING / sarı CAUTION; onaylanmamışsa yanıp söner
	if (State.Alerts.Num())
	{
		const bool bWarn = State.Alerts[0].Value;
		const FLinearColor C = bWarn ? Red : Amber;
		const float Bw = 420.f;
		const FVector2D P((Sz.X - Bw) * 0.5f, 90.f);
		const bool bShow = !State.bAlertUnacked || bBlink;
		SmvBox(Out, Layer, G, P, FVector2D(Bw, 40.f + 24.f * FMath::Min(3, State.Alerts.Num() - 1)), FLinearColor(0, 0, 0, 0.55f), W);
		if (bShow) SmvBox(Out, Layer + 1, G, P, FVector2D(Bw, 4.f), C, W);
		SmvText(Out, Layer + 2, G, P + FVector2D(14, 8), State.Alerts[0].Key, 20, bShow ? C : C * 0.6f);
		for (int32 i = 1; i < State.Alerts.Num() && i < 4; ++i)
			SmvText(Out, Layer + 2, G, P + FVector2D(14, 16 + 24.f * i), State.Alerts[i].Key, 13, State.Alerts[i].Value ? Red : Amber, false);
		SmvText(Out, Layer + 2, G, P + FVector2D(Bw - 120, 12), bWarn ? TEXT("MASTER WARN") : TEXT("MASTER CAUT"), 10, C, false);
	}

	// yarış / it dalaşı paneli (sol, bilgi kartının altında; sağ üstte dron paneli var)
	if (State.bRace || State.bDogfight)
	{
		const FVector2D P(14.f, 118.f);
		const int32 Rows = 3 + State.Standings.Num();
		SmvBox(Out, Layer, G, P, FVector2D(250, 20.f + 18.f * Rows + (State.bDogfight ? 22.f : 0.f)), Panel, W);
		SmvText(Out, Layer + 1, G, P + FVector2D(8, 4), State.RaceTitle, 12, Amber);
		SmvText(Out, Layer + 1, G, P + FVector2D(8, 24), State.RaceLine1, 11, Txt, false);
		SmvText(Out, Layer + 1, G, P + FVector2D(8, 42), State.RaceLine2, 11, Dim, false);
		float Ry = 60.f;
		for (const FString& S : State.Standings) { SmvText(Out, Layer + 1, G, P + FVector2D(8, Ry), S, 10, Dim, false); Ry += 18.f; }
		if (State.bDogfight)
		{
			SmvBox(Out, Layer + 1, G, P + FVector2D(8, Ry + 4), FVector2D(234, 8), FLinearColor(1, 1, 1, 0.15f), W);
			SmvBox(Out, Layer + 2, G, P + FVector2D(8, Ry + 4), FVector2D(234 * FMath::Clamp(State.HoldFrac, 0.f, 1.f), 8), State.bInCone ? Red : Amber, W);
			SmvText(Out, Layer + 1, G, P + FVector2D(8, Ry + 14), State.DfLine, 10, Txt, false);
		}
	}
	if (!State.Countdown.IsEmpty()) SmvText(Out, Layer + 3, G, FVector2D(Sz.X * 0.5f - 60.f, Sz.Y * 0.35f), State.Countdown, 64, Amber);

	// hedef kilidi
	if (State.bTargetOnScreen)
	{
		const FLinearColor C = State.bLocked ? Red : Green;
		const float R = State.bLocked ? 26.f : 34.f;
		const FVector2D P = State.TargetPos;
		const FVector2f Pf((float)P.X, (float)P.Y);
		TArray<FVector2f> L = { Pf + FVector2f(-R, -R), Pf + FVector2f(R, -R), Pf + FVector2f(R, R), Pf + FVector2f(-R, R), Pf + FVector2f(-R, -R) };
		FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), L, ESlateDrawEffect::None, C, true, 2.f);
		SmvText(Out, Layer + 2, G, P + FVector2D(R + 6, -R), State.TargetLabel, 11, C, false);
	}

	// PiP (sağ alt)
	if (State.Pip)
	{
		const FVector2D PSz(384, 216);
		const FVector2D P(Sz.X - PSz.X - 16, Sz.Y - PSz.Y - 80);   // alt bandın üstü
		SmvBox(Out, Layer, G, P - FVector2D(2, 2), PSz + FVector2D(4, 4), FLinearColor(0, 0, 0, 0.8f), W);
		FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(PSz, FSlateLayoutTransform(FVector2f(P))), &PipBrush, ESlateDrawEffect::None, FLinearColor::White);
		SmvText(Out, Layer + 2, G, P + FVector2D(6, 4), TEXT("GIMBAL"), 10, Green, false);
	}

	// FPV OSD teması
	if (State.bOsd)
	{
		SmvText(Out, Layer + 1, G, FVector2D(Sz.X * 0.18f, Sz.Y * 0.5f), State.OsdLeft, 16, FLinearColor::White);
		SmvText(Out, Layer + 1, G, FVector2D(Sz.X * 0.74f, Sz.Y * 0.5f), State.OsdRight, 16, FLinearColor::White);
		SmvText(Out, Layer + 1, G, FVector2D(Sz.X * 0.5f - 90.f, Sz.Y * 0.88f), State.OsdCenter, 14, FLinearColor::White);
		const FVector2D C = Sz * 0.5f;
		const FVector2f Cf((float)C.X, (float)C.Y);
		TArray<FVector2f> Cross = { Cf + FVector2f(-14, 0), Cf + FVector2f(14, 0) };
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Cross, ESlateDrawEffect::None, FLinearColor::White, true, 2.f);
	}

	if (State.bCrashed) SmvText(Out, Layer + 3, G, FVector2D(Sz.X * 0.5f - 150.f, Sz.Y * 0.45f), TEXT("ÇARPIŞMA"), 40, Red);
	// bildirim (toast) kabukta gösterilir (SSmvShell::ShowToast) — burada çift çizilmez
	if (!State.Paused.IsEmpty())
	{
		SmvBox(Out, Layer + 3, G, FVector2D(0, 0), Sz, FLinearColor(0, 0, 0, 0.35f), W);
		SmvText(Out, Layer + 4, G, FVector2D(Sz.X * 0.5f - 200.f, Sz.Y * 0.3f), State.Paused, 18, FLinearColor::White);
	}
	return Layer + 4;
}
