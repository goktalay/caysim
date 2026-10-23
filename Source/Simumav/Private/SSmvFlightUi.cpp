#include "SSmvFlightUi.h"
#include "SmvGameMode.h"
#include "SmvPilotPawn.h"
#include "SmvUiKit.h"
#include "SmvUiPrefs.h"
#include "SmvI18n.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"

// ---------------------------------------------------------------------------
// tema (SIMUMAV web: koyu lacivert cam zemin, neon mavi vurgu #38bdf8, yeşil #22c55e, kırmızı #ef4444, amber #ffbf47)
// ---------------------------------------------------------------------------

namespace SmvFuiTheme
{
	const FLinearColor Bg(0.028f, 0.042f, 0.075f, 0.80f);
	const FLinearColor HeadBg(0.055f, 0.085f, 0.15f, 0.94f);
	const FLinearColor Edge(0.56f, 0.67f, 0.9f, 0.20f);
	const FLinearColor Txt(0.90f, 0.93f, 0.95f, 1.f);
	const FLinearColor Dim(0.56f, 0.62f, 0.69f, 1.f);
	const FLinearColor Accent(0.22f, 0.74f, 0.97f, 1.f);
	const FLinearColor Green(0.13f, 0.77f, 0.37f, 1.f);
	const FLinearColor Red(0.94f, 0.27f, 0.27f, 1.f);
	const FLinearColor Amber(1.f, 0.75f, 0.28f, 1.f);
	const FLinearColor Blue(0.23f, 0.51f, 0.96f, 1.f);
	const FLinearColor Cyan(0.37f, 0.78f, 1.f, 1.f);
	const FLinearColor BtnGrey(0.16f, 0.21f, 0.30f, 1.f);

	FLinearColor Sev(int32 S)
	{
		if (S <= 3) return Red;
		if (S == 4) return Amber;
		if (S == 5) return Cyan;
		if (S >= 7) return Dim;
		return Txt;
	}
	FSlateFontInfo Font(int32 Size, bool bBold = false) { return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size); }
	FSlateFontInfo Mono(int32 Size) { return FCoreStyle::GetDefaultFontStyle("Mono", Size); }
	/** Dil anahtarı → metin (fui.*) */
	FString Loc(const TCHAR* Key) { return FSmvI18n::T(Key).ToString(); }
}

// ---------------------------------------------------------------------------
// çizim yardımcıları (OnPaint)
// ---------------------------------------------------------------------------

namespace SmvFuiDraw
{
	const FSlateBrush* White() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }

	void DrRect(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& P, const FVector2D& S, const FLinearColor& C, const FSlateBrush* B = nullptr)
	{
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2f(S), FSlateLayoutTransform(FVector2f(P))), B ? B : White(), ESlateDrawEffect::None, C);
	}

	void DrPoly(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const TArray<FVector2D>& Pts, const FLinearColor& C, float Thick)
	{
		if (Pts.Num() < 2) return;
		TArray<FVector2f> P;
		P.Reserve(Pts.Num());
		for (const FVector2D& V : Pts) P.Add(FVector2f((float)V.X, (float)V.Y));
		FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, C, true, Thick);
	}

	void DrSeg(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& A, const FVector2D& B, const FLinearColor& C, float Thick)
	{
		DrPoly(Out, L, G, { A, B }, C, Thick);
	}

	/** Metin; Align −1 sol, 0 orta, 1 sağ — dikeyde Pos.Y ortalanır. */
	void DrText(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FString& S, FVector2D Pos, const FSlateFontInfo& F, const FLinearColor& C, int32 Align = -1)
	{
		if (S.IsEmpty()) return;
		const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D Sz = M->Measure(S, F);
		if (Align == 0) Pos.X -= Sz.X * 0.5;
		else if (Align > 0) Pos.X -= Sz.X;
		Pos.Y -= Sz.Y * 0.5;
		FSlateDrawElement::MakeText(Out, L, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(Pos))), S, F, ESlateDrawEffect::None, C);
	}

	FVector2D DrRot(const FVector2D& P, float A)
	{
		const float c = FMath::Cos(A), s = FMath::Sin(A);
		return FVector2D(P.X * c - P.Y * s, P.X * s + P.Y * c);
	}

	/** Yay (ekran açısı derece; 0 = sağ, saat yönü artar — y aşağı). */
	void DrArc(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& C, float R, float A0, float A1, const FLinearColor& Col, float Thick)
	{
		const int32 N = FMath::Clamp((int32)(FMath::Abs(A1 - A0) / 4.f), 2, 120);
		TArray<FVector2D> P;
		for (int32 i = 0; i <= N; ++i)
		{
			const float A = FMath::DegreesToRadians(FMath::Lerp(A0, A1, (float)i / N));
			P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		DrPoly(Out, L, G, P, Col, Thick);
	}

	FString Pad3(float Deg)
	{
		int32 D = FMath::RoundToInt(Deg) % 360;
		if (D < 0) D += 360;
		return FString::Printf(TEXT("%03d"), D);
	}
}

// ---------------------------------------------------------------------------
// SSmvPaintBox — OnPaint ile çizilen yaprak bileşen (PFD, kadranlar, çubuklar); isteğe bağlı fare sürükleme
// ---------------------------------------------------------------------------

class SSmvPaintBox : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvPaintBox) : _Size(FVector2D(100, 100)) {}
		SLATE_ARGUMENT(FVector2D, Size)
	SLATE_END_ARGS()

	TFunction<void(const FGeometry&, FSlateWindowElementList&, int32)> Painter;
	/** Faz: 0 bas, 1 sürükle, 2 bırak; konum yerel piksel. */
	TFunction<void(const FGeometry&, const FVector2D&, int32)> OnDrag;

	void Construct(const FArguments& A) { Size = A._Size; }

	virtual FVector2D ComputeDesiredSize(float) const override { return Size; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		if (Painter) Painter(G, Out, Layer);
		return Layer + 14;   // çizimler Layer..Layer+12 katmanlarını kullanır
	}

	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
	{
		if (!OnDrag) return FReply::Unhandled();
		OnDrag(G, FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition())), 0);
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override
	{
		if (!OnDrag || !HasMouseCapture()) return FReply::Unhandled();
		OnDrag(G, FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition())), 1);
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override
	{
		if (!OnDrag || !HasMouseCapture()) return FReply::Unhandled();
		OnDrag(G, FVector2D(G.AbsoluteToLocal(E.GetScreenSpacePosition())), 2);
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& E) override
	{
		if (OnDrag) OnDrag(FGeometry(), FVector2D::ZeroVector, 2);
	}

private:
	FVector2D Size = FVector2D(100, 100);
};

// ---------------------------------------------------------------------------
// PFD — yapay ufuk, pitch merdiveni, roll yayı, hız / irtifa şeritleri, yön şeridi
// ---------------------------------------------------------------------------

static void SmvPaintPfd(const FGeometry& G, FSlateWindowElementList& Out, int32 L, const FSmvFlightUiState& S)
{
	using namespace SmvFuiDraw;
	using namespace SmvFuiTheme;
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const float W = (float)Sz.X, H = (float)Sz.Y;
	Out.PushClip(FSlateClippingZone(G));
	const FLinearColor Sky(0.13f, 0.38f, 0.70f, 1.f), Ground(0.45f, 0.29f, 0.14f, 1.f), Wht(1.f, 1.f, 1.f, 0.92f);
	DrRect(Out, L, G, FVector2D::ZeroVector, Sz, Sky);
	const float Ppd = H / 48.f;   // piksel / derece
	const float Roll = S.Roll, PitchDeg = FMath::RadiansToDegrees(S.Pitch);
	const FVector2D C(W * 0.5f, H * 0.46f);
	// ufuk: zemin = (p − H0)·N > 0; N roll ile döner (sağa yatış → ufuk ekranda saat yönünün tersine)
	const FVector2D N = DrRot(FVector2D(0, 1), -Roll);
	const FVector2D H0 = C + DrRot(FVector2D(0, PitchDeg * Ppd), -Roll);
	for (float y = 0.f; y < H; y += 2.f)
	{
		const float Yc = y + 1.f;
		float X0 = 0.f, X1 = W;
		if (FMath::Abs(N.X) < 1e-4)
		{
			if ((Yc - H0.Y) * N.Y <= 0.f) continue;
		}
		else
		{
			const float Xs = (float)(H0.X - (Yc - H0.Y) * N.Y / N.X);
			if (N.X > 0) X0 = FMath::Max(0.f, Xs); else X1 = FMath::Min(W, Xs);
			if (X1 <= X0) continue;
		}
		DrSeg(Out, L + 1, G, FVector2D(X0, Yc), FVector2D(X1, Yc), Ground, 2.6f);
	}
	// ufuk çizgisi + pitch merdiveni
	{
		const FVector2D Dir = DrRot(FVector2D(1, 0), -Roll);
		DrSeg(Out, L + 2, G, H0 - Dir * W, H0 + Dir * W, Wht, 1.5f);
		for (int32 Deg = -40; Deg <= 40; Deg += 5)
		{
			if (Deg == 0) continue;
			const float Off = (PitchDeg - Deg) * Ppd;
			if (FMath::Abs(Off) > H * 0.40f) continue;
			const float Hw = Deg % 10 == 0 ? 30.f : 14.f;
			const FVector2D P0 = C + DrRot(FVector2D(-Hw, Off), -Roll), P1 = C + DrRot(FVector2D(Hw, Off), -Roll);
			DrSeg(Out, L + 2, G, P0, P1, Wht, 1.2f);
			if (Deg % 10 == 0)
			{
				const FString Lb = FString::FromInt(FMath::Abs(Deg));
				DrText(Out, L + 3, G, Lb, C + DrRot(FVector2D(-Hw - 11.f, Off), -Roll), Mono(7), Wht, 0);
				DrText(Out, L + 3, G, Lb, C + DrRot(FVector2D(Hw + 11.f, Off), -Roll), Mono(7), Wht, 0);
			}
		}
	}
	// roll yayı (sabit ölçek) + ufukla dönen işaretçi
	{
		const float Rr = H * 0.40f;
		DrArc(Out, L + 3, G, C, Rr, -150.f, -30.f, Wht, 1.2f);
		for (const float A : { -60.f, -45.f, -30.f, -20.f, -10.f, 0.f, 10.f, 20.f, 30.f, 45.f, 60.f })
		{
			const float Rad = FMath::DegreesToRadians(-90.f + A);
			const float Len = (FMath::Abs(A) == 30.f || FMath::Abs(A) == 60.f || A == 0.f) ? 9.f : 5.f;
			const FVector2D U(FMath::Cos(Rad), FMath::Sin(Rad));
			DrSeg(Out, L + 3, G, C + U * Rr, C + U * (Rr + Len), Wht, 1.2f);
		}
		const float Pa = FMath::DegreesToRadians(-90.f) - Roll;
		const FVector2D U(FMath::Cos(Pa), FMath::Sin(Pa)), Tn(-U.Y, U.X);
		const FVector2D Tip = C + U * (Rr - 1.f);
		DrPoly(Out, L + 4, G, { Tip, Tip - U * 9.f + Tn * 6.f, Tip - U * 9.f - Tn * 6.f, Tip }, Amber, 2.f);
	}
	// uçak sembolü (sabit)
	{
		const FLinearColor Y(1.f, 0.85f, 0.1f, 1.f);
		DrPoly(Out, L + 5, G, { C + FVector2D(-62, 0), C + FVector2D(-24, 0), C + FVector2D(-24, 7) }, FLinearColor(0, 0, 0, 0.6f), 5.f);
		DrPoly(Out, L + 5, G, { C + FVector2D(62, 0), C + FVector2D(24, 0), C + FVector2D(24, 7) }, FLinearColor(0, 0, 0, 0.6f), 5.f);
		DrPoly(Out, L + 6, G, { C + FVector2D(-62, 0), C + FVector2D(-24, 0), C + FVector2D(-24, 7) }, Y, 3.f);
		DrPoly(Out, L + 6, G, { C + FVector2D(62, 0), C + FVector2D(24, 0), C + FVector2D(24, 7) }, Y, 3.f);
		DrRect(Out, L + 6, G, C - FVector2D(3, 3), FVector2D(6, 6), Y);
	}
	const FLinearColor Tape(0.f, 0.f, 0.f, 0.38f);
	const float HdgH = 22.f;
	const float TapeH = H - HdgH;
	// hız şeridi (sol, km/h)
	{
		const float Tw = 46.f, Ppu = 2.4f, V = S.Gs * 3.6f;
		DrRect(Out, L + 7, G, FVector2D(0, 0), FVector2D(Tw, TapeH), Tape);
		const int32 Lo = FMath::FloorToInt((V - TapeH / 2.f / Ppu) / 5.f) * 5;
		for (int32 v = FMath::Max(0, Lo); v <= V + TapeH / 2.f / Ppu; v += 5)
		{
			const float Y = TapeH * 0.5f - (v - V) * Ppu;
			if (Y < 4.f || Y > TapeH - 4.f) continue;
			DrSeg(Out, L + 8, G, FVector2D(Tw - (v % 10 == 0 ? 8.f : 4.f), Y), FVector2D(Tw, Y), Wht, 1.f);
			if (v % 20 == 0) DrText(Out, L + 8, G, FString::FromInt(v), FVector2D(Tw - 11.f, Y), Mono(7), Wht, 1);
		}
		const float Cy = TapeH * 0.5f;
		DrRect(Out, L + 9, G, FVector2D(1, Cy - 10.f), FVector2D(Tw + 4.f, 20.f), FLinearColor(0.02f, 0.03f, 0.04f, 0.95f));
		DrPoly(Out, L + 10, G, { FVector2D(1, Cy - 10.f), FVector2D(Tw + 4.f, Cy - 10.f), FVector2D(Tw + 4.f, Cy + 10.f), FVector2D(1, Cy + 10.f), FVector2D(1, Cy - 10.f) }, Wht, 1.f);
		DrText(Out, L + 10, G, FString::Printf(TEXT("%.0f"), V), FVector2D(Tw - 2.f, Cy), Mono(10), Wht, 1);
		DrText(Out, L + 8, G, TEXT("km/h"), FVector2D(Tw * 0.5f, 7.f), Mono(6), Dim, 0);
	}
	// irtifa şeridi (sağ, m — kalkış noktasına göre)
	{
		const float Tw = 50.f, Ppu = 3.2f, A = S.AltRel, X0 = W - Tw;
		DrRect(Out, L + 7, G, FVector2D(X0, 0), FVector2D(Tw, TapeH), Tape);
		const int32 Lo = FMath::FloorToInt((A - TapeH / 2.f / Ppu) / 5.f) * 5;
		for (int32 a = Lo; a <= A + TapeH / 2.f / Ppu; a += 5)
		{
			const float Y = TapeH * 0.5f - (a - A) * Ppu;
			if (Y < 4.f || Y > TapeH - 4.f) continue;
			DrSeg(Out, L + 8, G, FVector2D(X0, Y), FVector2D(X0 + (a % 10 == 0 ? 8.f : 4.f), Y), Wht, 1.f);
			if (a % 10 == 0) DrText(Out, L + 8, G, FString::FromInt(a), FVector2D(X0 + 11.f, Y), Mono(7), Wht, -1);
		}
		const float Cy = TapeH * 0.5f;
		DrRect(Out, L + 9, G, FVector2D(X0 - 4.f, Cy - 10.f), FVector2D(Tw + 3.f, 20.f), FLinearColor(0.02f, 0.03f, 0.04f, 0.95f));
		DrPoly(Out, L + 10, G, { FVector2D(X0 - 4.f, Cy - 10.f), FVector2D(W - 1.f, Cy - 10.f), FVector2D(W - 1.f, Cy + 10.f), FVector2D(X0 - 4.f, Cy + 10.f), FVector2D(X0 - 4.f, Cy - 10.f) }, Wht, 1.f);
		DrText(Out, L + 10, G, FString::Printf(TEXT("%.1f"), A), FVector2D(X0 + 2.f, Cy), Mono(10), Wht, -1);
		DrText(Out, L + 8, G, TEXT("ALT m"), FVector2D(X0 + Tw * 0.5f, 7.f), Mono(6), Dim, 0);
		// dikey hız çubuğu
		const float Vs = FMath::Clamp(S.Climb, -10.f, 10.f);
		const float Bh = Vs / 10.f * (TapeH * 0.4f);
		DrRect(Out, L + 8, G, FVector2D(X0 - 8.f, Bh > 0 ? Cy - Bh : Cy), FVector2D(3.f, FMath::Abs(Bh)), Vs >= 0 ? Green : Amber);
	}
	// yön şeridi (alt)
	{
		const float Y0 = H - HdgH, Ppu = 2.2f, Hd = S.Heading;
		DrRect(Out, L + 7, G, FVector2D(0, Y0), FVector2D(W, HdgH), FLinearColor(0.f, 0.f, 0.f, 0.55f));
		const int32 Lo = FMath::FloorToInt((Hd - W / 2.f / Ppu) / 5.f) * 5;
		for (int32 d = Lo; d <= Hd + W / 2.f / Ppu; d += 5)
		{
			const float X = W * 0.5f + (d - Hd) * Ppu;
			const int32 Dn = ((d % 360) + 360) % 360;
			DrSeg(Out, L + 8, G, FVector2D(X, Y0), FVector2D(X, Y0 + (Dn % 10 == 0 ? 6.f : 3.f)), Wht, 1.f);
			if (Dn % 30 == 0)
			{
				const FString Lb = Dn == 0 ? TEXT("N") : Dn == 90 ? TEXT("E") : Dn == 180 ? TEXT("S") : Dn == 270 ? TEXT("W") : FString::FromInt(Dn / 10);
				DrText(Out, L + 8, G, Lb, FVector2D(X, Y0 + 14.f), Mono(7), Dn % 90 == 0 ? Amber : Wht, 0);
			}
		}
		DrRect(Out, L + 9, G, FVector2D(W * 0.5f - 18.f, Y0 - 15.f), FVector2D(36.f, 15.f), FLinearColor(0.02f, 0.03f, 0.04f, 0.95f));
		DrText(Out, L + 10, G, Pad3(Hd), FVector2D(W * 0.5f, Y0 - 7.5f), Mono(9), Wht, 0);
	}
	// kip + ARM, dikey hız
	DrText(Out, L + 10, G, S.Mode.IsEmpty() ? TEXT("—") : S.Mode, FVector2D(52.f, 12.f), Font(9, true), Wht, -1);
	DrText(Out, L + 10, G, S.bArmed ? TEXT("ARMED") : TEXT("DISARMED"), FVector2D(52.f, 26.f), Font(8, true), S.bArmed ? Green : FLinearColor(1.f, 0.55f, 0.55f, 1.f), -1);
	DrText(Out, L + 10, G, FString::Printf(TEXT("VS %+.1f"), S.Climb), FVector2D(W - 56.f, 12.f), Mono(8), Wht, 1);
	if (!S.bTele)
	{
		DrRect(Out, L + 11, G, FVector2D::ZeroVector, Sz, FLinearColor(0.02f, 0.03f, 0.04f, 0.72f));
		DrText(Out, L + 12, G, SmvFuiTheme::Loc(TEXT("fui.waiting_tele")), FVector2D(W * 0.5f, H * 0.5f), Font(10, true), Dim, 0);
	}
	Out.PopClip();
}

// ---------------------------------------------------------------------------
// güç kadranları — otomobil tarzı analog (240° süpürme; kırmızı bölge; dijital değer)
// ---------------------------------------------------------------------------

struct FSmvDial
{
	FString Title, Value, Sub;
	float Frac = 0.f;                  // 0..1
	float RedA = -1.f, RedB = -1.f;    // kırmızı bölge (kesir)
	TArray<FString> Labels;            // ana çentik etiketleri (eşit aralıklı)
	bool bOn = true, bDanger = false;
};

/** Kadran tasarımı (web gtheme): 0 uçak kokpiti, 1 klasik otomobil, 2 dijital cam (MFD karosu), 3 minimal, 4 retro askerî */
static int32 SmvDialTheme()
{
	const FString& T = FSmvUiPrefs::Get().DialTheme;
	return T == TEXT("classic") ? 1 : T == TEXT("glass") ? 2 : T == TEXT("minimal") ? 3 : T == TEXT("military") ? 4 : 0;
}

static void SmvPaintDial(const FGeometry& G, FSlateWindowElementList& Out, int32 L, const FVector2D& C, float R, const FSmvDial& D, const FSlateBrush* Circle)
{
	using namespace SmvFuiDraw;
	using namespace SmvFuiTheme;
	const int32 Th = SmvDialTheme();
	const bool bBlink = FMath::Fmod(FPlatformTime::Seconds(), 0.9) < 0.45;
	const float Alpha = D.bOn ? 1.f : 0.35f;
	if (Th == 2)
	{
		// dijital cam: MFD karosu — başlık, büyük sayısal değer, seviye şeridi
		const FVector2D P0 = C - FVector2D(R, R * 0.78f), Sz(2 * R, R * 1.56f);
		DrRect(Out, L, G, P0, Sz, FLinearColor(0.02f, 0.05f, 0.08f, 0.82f), nullptr);
		DrPoly(Out, L + 1, G, { P0, P0 + FVector2D(Sz.X, 0), P0 + Sz, P0 + FVector2D(0, Sz.Y), P0 }, D.bDanger && bBlink ? Red : Accent.CopyWithNewOpacity(0.55f), 1.f);
		DrText(Out, L + 2, G, D.Title, P0 + FVector2D(8.f, 9.f), Font(7, true), Accent, -1);
		DrText(Out, L + 2, G, D.Value, C + FVector2D(0, -R * 0.05f), Mono(R > 50.f ? 20 : 15), D.bDanger ? Red : Txt.CopyWithNewOpacity(Alpha), 0);
		if (!D.Sub.IsEmpty()) DrText(Out, L + 2, G, D.Sub, C + FVector2D(0, R * 0.30f), Mono(7), Dim, 0);
		const FVector2D B0 = P0 + FVector2D(8.f, Sz.Y - 12.f);
		const float Bw = (float)Sz.X - 16.f;
		DrRect(Out, L + 2, G, B0, FVector2D(Bw, 5.f), FLinearColor(1, 1, 1, 0.1f), nullptr);
		if (D.RedA >= 0.f && D.RedB > D.RedA) DrRect(Out, L + 3, G, B0 + FVector2D(Bw * D.RedA, 0), FVector2D(Bw * (D.RedB - D.RedA), 5.f), Red.CopyWithNewOpacity(0.35f), nullptr);
		DrRect(Out, L + 4, G, B0, FVector2D(Bw * FMath::Clamp(D.bOn ? D.Frac : 0.f, 0.f, 1.f), 5.f), D.bDanger ? Red : Accent, nullptr);
		return;
	}
	// renk takımı
	FLinearColor Face, Ring, Tick, Lab, Needle, Hub, ValC;
	switch (Th)
	{
	case 1: Face = FLinearColor(0.92f, 0.92f, 0.88f, 0.96f); Ring = FLinearColor(0.25f, 0.25f, 0.27f, 1.f); Tick = FLinearColor(0.05f, 0.05f, 0.06f, 1.f);
		Lab = FLinearColor(0.1f, 0.1f, 0.12f, 1.f); Needle = FLinearColor(0.85f, 0.18f, 0.08f, 1.f); Hub = FLinearColor(0.1f, 0.1f, 0.1f, 1.f); ValC = FLinearColor(0.08f, 0.08f, 0.1f, 1.f); break;
	case 3: Face = FLinearColor(0, 0, 0, 0.18f); Ring = FLinearColor(1, 1, 1, 0.35f); Tick = FLinearColor(1, 1, 1, 0.55f);
		Lab = Dim; Needle = Accent; Hub = Accent; ValC = Txt; break;
	case 4: Face = FLinearColor(0.11f, 0.14f, 0.07f, 0.94f); Ring = FLinearColor(0.32f, 0.38f, 0.2f, 1.f); Tick = FLinearColor(0.45f, 1.f, 0.35f, 1.f);
		Lab = FLinearColor(0.45f, 1.f, 0.35f, 0.8f); Needle = FLinearColor(0.55f, 1.f, 0.4f, 1.f); Hub = FLinearColor(0.3f, 0.36f, 0.2f, 1.f); ValC = FLinearColor(0.55f, 1.f, 0.4f, 1.f); break;
	default: Face = FLinearColor(0.035f, 0.05f, 0.065f, 0.90f); Ring = FLinearColor(1, 1, 1, 0.16f); Tick = FLinearColor::White;
		Lab = Dim; Needle = Accent; Hub = FLinearColor(0.85f, 0.87f, 0.9f, 1.f); ValC = Txt; break;
	}
	const float A0 = 150.f, Sweep = 240.f;
	auto Ang = [A0, Sweep](float F) { return A0 + Sweep * FMath::Clamp(F, 0.f, 1.f); };
	DrRect(Out, L, G, C - FVector2D(R, R), FVector2D(2 * R, 2 * R), Face, Circle);
	DrArc(Out, L + 1, G, C, R - 1.f, 0.f, 360.f, D.bDanger && bBlink ? Red : Ring, D.bDanger ? 2.5f : (Th == 1 || Th == 4 ? 3.f : 1.5f));
	if (Th == 3) DrArc(Out, L + 1, G, C, R - 6.f, Ang(0.f), Ang(D.bOn ? D.Frac : 0.f), (D.bDanger ? Red : Accent).CopyWithNewOpacity(0.8f * Alpha), 3.f);
	if (D.RedA >= 0.f && D.RedB > D.RedA) DrArc(Out, L + 1, G, C, R - 7.f, Ang(D.RedA), Ang(D.RedB), Red.CopyWithNewOpacity(0.85f * Alpha), Th == 3 ? 2.f : 4.f);
	const int32 Major = FMath::Max(1, D.Labels.Num() - 1);
	for (int32 i = 0; i <= Major * 5; ++i)
	{
		const bool bMaj = i % 5 == 0;
		if (Th == 3 && !bMaj) continue;
		const float F = (float)i / (Major * 5);
		const float Rad = FMath::DegreesToRadians(Ang(F));
		const FVector2D U(FMath::Cos(Rad), FMath::Sin(Rad));
		DrSeg(Out, L + 2, G, C + U * (R - 3.f), C + U * (R - (bMaj ? 12.f : 7.f)), Tick.CopyWithNewOpacity(Tick.A * (bMaj ? 0.85f : 0.45f) * Alpha), bMaj ? 2.f : 1.f);
		if (bMaj && D.Labels.IsValidIndex(i / 5) && R > 50.f) DrText(Out, L + 2, G, D.Labels[i / 5], C + U * (R - 22.f), Mono(7), Lab.CopyWithNewOpacity(Lab.A * Alpha), 0);
		else if (bMaj && D.Labels.IsValidIndex(i / 5) && (i == 0 || i == Major * 5)) DrText(Out, L + 2, G, D.Labels[i / 5], C + U * (R - 20.f), Mono(7), Lab.CopyWithNewOpacity(Lab.A * Alpha), 0);
	}
	// ibre
	const float Nr = FMath::DegreesToRadians(Ang(D.bOn ? D.Frac : 0.f));
	const FVector2D U(FMath::Cos(Nr), FMath::Sin(Nr));
	if (Th != 3) DrSeg(Out, L + 3, G, C - U * 8.f, C + U * (R - 10.f), FLinearColor(0, 0, 0, 0.6f), 4.5f);
	DrSeg(Out, L + 4, G, C - U * 8.f, C + U * (R - 10.f), D.bDanger ? Red : Needle, Th == 3 ? 1.5f : 2.5f);
	DrRect(Out, L + 5, G, C - FVector2D(5, 5), FVector2D(10, 10), Hub, Circle);
	DrText(Out, L + 5, G, D.Title, C + FVector2D(0, -R * 0.36f), Font(R > 50.f ? 8 : 7, true), Lab, 0);
	DrText(Out, L + 5, G, D.Value, C + FVector2D(0, R * 0.42f), Mono(R > 50.f ? 13 : 10), D.bDanger ? Red : ValC.CopyWithNewOpacity(Alpha), 0);
	if (!D.Sub.IsEmpty()) DrText(Out, L + 5, G, D.Sub, C + FVector2D(0, R * 0.70f), Mono(7), Lab, 0);
}

// ---------------------------------------------------------------------------
// sanal çubuk
// ---------------------------------------------------------------------------

static void SmvPaintStick(const FGeometry& G, FSlateWindowElementList& Out, int32 L, const FVector2D& V, bool bActive, const FString& Lx, const FString& Ly,
	const FSlateBrush* Card, const FSlateBrush* Circle)
{
	using namespace SmvFuiDraw;
	using namespace SmvFuiTheme;
	const FVector2D Sz = FVector2D(G.GetLocalSize());
	const FVector2D C = Sz * 0.5;
	const float R = (float)FMath::Min(Sz.X, Sz.Y) * 0.5f - 16.f;
	DrRect(Out, L, G, FVector2D::ZeroVector, Sz, FLinearColor::White, Card);
	DrArc(Out, L + 1, G, C, R, 0.f, 360.f, FLinearColor(1, 1, 1, 0.14f), 1.f);
	DrArc(Out, L + 1, G, C, R * 0.5f, 0.f, 360.f, FLinearColor(1, 1, 1, 0.07f), 1.f);
	DrSeg(Out, L + 1, G, C - FVector2D(R, 0), C + FVector2D(R, 0), FLinearColor(1, 1, 1, 0.12f), 1.f);
	DrSeg(Out, L + 1, G, C - FVector2D(0, R), C + FVector2D(0, R), FLinearColor(1, 1, 1, 0.12f), 1.f);
	const FVector2D K = C + FVector2D(FMath::Clamp(V.X, -1.0, 1.0) * R, -FMath::Clamp(V.Y, -1.0, 1.0) * R);
	DrSeg(Out, L + 2, G, C, K, FLinearColor(1, 1, 1, 0.55f), 3.f);   // kol
	DrRect(Out, L + 2, G, C - FVector2D(4, 4), FVector2D(8, 8), FLinearColor(1, 1, 1, 0.5f), Circle);
	const FLinearColor Kc = bActive ? Accent : FLinearColor(0.88f, 0.91f, 0.95f, 1.f);
	DrRect(Out, L + 3, G, K - FVector2D(12, 12), FVector2D(24, 24), FLinearColor(0, 0, 0, 0.5f), Circle);
	DrRect(Out, L + 4, G, K - FVector2D(10, 10), FVector2D(20, 20), Kc, Circle);
	DrText(Out, L + 4, G, Ly, FVector2D(C.X, 8.f), Font(7, true), Dim, 0);
	DrText(Out, L + 4, G, Lx, FVector2D(Sz.X - 6.f, C.Y), Font(7, true), Dim, 1);
}

// ---------------------------------------------------------------------------
// SSmvFlightUi
// ---------------------------------------------------------------------------

void SSmvFlightUi::Construct(const FArguments& InArgs)
{
	using namespace SmvFuiTheme;
	GM = InArgs._GameMode;
	CardBrush = FSlateRoundedBoxBrush(Bg, 8.f, Edge, 1.f);
	HeadBrush = FSlateRoundedBoxBrush(HeadBg, 8.f);
	HeadBrush.OutlineSettings.CornerRadii = FVector4(8.f, 8.f, 0.f, 0.f);   // yalnız üst köşeler (sol üst, sağ üst, sağ alt, sol alt)
	ChipBrush = FSlateRoundedBoxBrush(FLinearColor::White, 10.f);
	TileBrush = FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.045f), 6.f);
	ConsoleBrush = FSlateRoundedBoxBrush(FLinearColor(0.02f, 0.03f, 0.04f, 0.85f), 6.f);
	BarBrush = FSlateRoundedBoxBrush(Bg, 12.f, Edge, 1.f);
	CircleBrush = FSlateRoundedBoxBrush(FLinearColor::White);   // yarı yükseklik yarıçapı → kare alanda daire
	{
		const FSlateBrush N = FSlateRoundedBoxBrush(FLinearColor(0.82f, 0.82f, 0.82f, 1.f), 7.f);
		const FSlateBrush Hv = FSlateRoundedBoxBrush(FLinearColor::White, 7.f);
		const FSlateBrush Pr = FSlateRoundedBoxBrush(FLinearColor(0.62f, 0.62f, 0.62f, 1.f), 7.f);
		BtnStyle = FButtonStyle().SetNormal(N).SetHovered(Hv).SetPressed(Pr).SetDisabled(N)
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0, 1, 0, 0));
	}

	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		// çarpma parlaması (en altta, sahnenin üstünde)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([this]()
			{
				const double K = FMath::Clamp(1.0 - (FPlatformTime::Seconds() - FlashAt) / 0.9, 0.0, 1.0);
				FLinearColor C = FlashC; C.A *= (float)(K * K);
				return FSlateColor(C);
			})
		]
		// sol üst: bilgi kartı
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(14.f)
		[
			SNew(SVerticalBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SVerticalBox::Slot().AutoHeight()[InfoCard()]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)[WindCard()]
		]
		// sağ üst: dron paneli
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(14.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return bPanel ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				DronePanel()
			]
		]
		// sol alt: ileti akışı + sanal kumanda
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(14.f, 0.f, 0.f, 74.f)
		[
			SNew(SVerticalBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[Feed()]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([this]() { return bSticks && !St.bMenuOpen ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
				[
					Sticks()
				]
			]
		]
		// alt orta: güç kadranları
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 70.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return St.bHideGauges || St.bMenuOpen || (St.bPlane && FSmvUiPrefs::Get().bCockpit) || FSmvUiPrefs::Get().DialTheme == TEXT("none") ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
			[
				Gauges()
			]
		]
		// göstergesiz: küçük "Göstergeler" düğmesi (ya da G) geri açar
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 76.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return !St.bMenuOpen && !St.bHideGauges && !(St.bPlane && FSmvUiPrefs::Get().bCockpit) && FSmvUiPrefs::Get().DialTheme == TEXT("none") ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Btn(TAttribute<FText>::CreateLambda([]() { return FSmvI18n::T(TEXT("gtheme.none_show")); }), [this]() { ToggleCockpit(); }, []() { return FLinearColor(0.6f, 0.7f, 0.8f, 0.85f); })
			]
		]
		// alt bant
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 12.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return St.bMenuOpen ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })
			[
				BottomBar()
			]
		]
		// kokpit gösterge paneli (sabit kanat / VTOL; kadranların yerine)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 70.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return St.bPlane && FSmvUiPrefs::Get().bCockpit && !St.bMenuOpen && !St.bHideGauges ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				Cockpit()
			]
		]
		// giriş sekansı 3-2-1
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 110.f, 0.f, 0.f)
		[
			Intro()
		]
		// FPV parkuru şeridi
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 14.f, 0.f, 0.f)
		[
			FpvBar()
		]
		// ilk uçuş ipuçları
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 104.f, 0.f, 0.f)
		[
			Tips()
		]
		// uçuş modu radyal tekerleği (ortada)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return bModeMenu && !St.bMenuOpen ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				ModeMenu()
			]
		]
	];
}

void SSmvFlightUi::Act(const FString& Name, const FString& Arg)
{
	if (GM.IsValid()) GM->UiAction(Name, Arg);
}

void SSmvFlightUi::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// dönüş hızı (derece/sn; kokpit dönüş göstergesi) — yön farkından, yumuşatılmış
	if (InDeltaTime > 0.f)
	{
		if (!bHdgInit) { PrevHdg = St.Heading; bHdgInit = true; }
		float D = St.Heading - PrevHdg;
		if (D > 180.f) D -= 360.f; else if (D < -180.f) D += 360.f;
		PrevHdg = St.Heading;
		TurnRate = FMath::Lerp(TurnRate, FMath::Clamp(D / InDeltaTime, -30.f, 30.f), FMath::Clamp(InDeltaTime * 3.f, 0.f, 1.f));
	}
	// giriş sekansı bipleri (3, 2, 1, BAŞLA)
	if (St.bIntroAllowed && St.bTele && St.FlightS < 4.5)
	{
		const int32 Sec = St.FlightS < 1.0 ? 3 : St.FlightS < 2.0 ? 2 : St.FlightS < 3.0 ? 1 : St.FlightS < 3.7 ? 0 : -1;
		if (Sec != LastIntroSec && Sec >= 0) Act(TEXT("IntroBeep"), Sec == 0 ? TEXT("go") : TEXT("count"));
		LastIntroSec = Sec;
	}
	// ilk uçuş ipuçları (bir kez; 22 sn ya da kapatınca)
	if (!bTips && !FSmvUiPrefs::Get().bFlightTipsDone && St.FlightS > 4.0) { bTips = true; TipsUntil = FPlatformTime::Seconds() + 22.0; }
	if (bTips && FPlatformTime::Seconds() > TipsUntil) { bTips = false; FSmvUiPrefs::Get().bFlightTipsDone = true; FSmvUiPrefs::Get().Save(); }
}

TSharedRef<SWidget> SSmvFlightUi::Btn(TAttribute<FText> Label, TFunction<void()> OnClick, TFunction<FLinearColor()> Color, float MinW)
{
	return SNew(SBox).MinDesiredWidth(MinW)
	[
		SNew(SButton)
		.ButtonStyle(&BtnStyle)
		.IsFocusable(false)   // klavye odağı oyunda kalsın (Boşluk/Enter düğmeye gitmesin)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ContentPadding(FMargin(12.f, 7.f))
		.ButtonColorAndOpacity_Lambda([Color]() { return FSlateColor(Color ? Color() : SmvFuiTheme::BtnGrey); })
		.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
		[
			SNew(STextBlock).Text(Label).Font(SmvFuiTheme::Font(10, true)).ColorAndOpacity(FLinearColor::White)
			.ShadowOffset(FVector2D(0, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.45f))
		]
	];
}

TSharedRef<SWidget> SSmvFlightUi::Chip(TAttribute<FText> Label, TFunction<FLinearColor()> Color)
{
	return SNew(SBorder)
		.BorderImage(&ChipBrush)
		.BorderBackgroundColor_Lambda([Color]() { return FSlateColor(Color().CopyWithNewOpacity(0.22f)); })
		.Padding(FMargin(8.f, 2.f))
		[
			SNew(STextBlock).Text(Label).Font(SmvFuiTheme::Font(8, true)).ColorAndOpacity_Lambda([Color]() { return FSlateColor(Color()); })
		];
}

TSharedRef<SWidget> SSmvFlightUi::InfoCard()
{
	using namespace SmvFuiTheme;
	return SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(12.f, 9.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("SIMUMAV"))).Font(Font(11, true)).ColorAndOpacity(Accent)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Font(11, true)).ColorAndOpacity(Txt)
				.Text_Lambda([this]() { return FText::FromString(St.Account); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)[Chip(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(St.Backend); }), []() { return Dim; })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)[Chip(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(St.Mode.IsEmpty() ? TEXT("—") : St.Mode); }), []() { return Accent; })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)[Chip(TAttribute<FText>::CreateLambda([this]()
				{ return FText::FromString(St.bMotorCut ? Loc(TEXT("fui.motor_cut")) : St.bArmed ? FString(TEXT("ARMED")) : FString(TEXT("DISARMED"))); }),
				[this]() { return St.bMotorCut ? Amber : St.bArmed ? Green : Dim; })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)[Chip(TAttribute<FText>::CreateLambda([this]()
				{
					const FString Fx = St.GpsFix >= 6 ? FString(TEXT("RTK")) : St.GpsFix >= 3 ? FString(TEXT("3D")) : St.GpsFix == 2 ? FString(TEXT("2D")) : Loc(TEXT("fui.gps_none"));
					return FText::FromString(St.Sats >= 0 ? FString::Printf(TEXT("GPS %s · %d"), *Fx, St.Sats) : FString::Printf(TEXT("GPS %s"), *Fx));
				}), [this]() { return St.GpsFix >= 3 ? Green : Amber; })]
			+ SHorizontalBox::Slot().AutoWidth()[Chip(TAttribute<FText>::CreateLambda([this]()
				{ const int32 T = (int32)St.FlightS; return FText::FromString(FString::Printf(TEXT("T+ %02d:%02d"), T / 60, T % 60)); }), []() { return Txt; })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
		[
			SNew(STextBlock).Font(Font(8)).ColorAndOpacity(Dim)
			.Text_Lambda([this]() { return FText::FromString(St.MpLine.IsEmpty() ? FString() : TEXT("Mission Planner: ") + St.MpLine); })
			.Visibility_Lambda([this]() { return St.MpLine.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
		]
	];
}

TSharedRef<SWidget> SSmvFlightUi::TeleTile(const FString& Label, TFunction<FString()> Value, TFunction<FLinearColor()> Color)
{
	using namespace SmvFuiTheme;
	return SNew(SBorder).BorderImage(&TileBrush).Padding(FMargin(7.f, 5.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(7, true)).ColorAndOpacity(Dim)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
		[
			SNew(STextBlock).Font(Mono(11))
			.Text_Lambda([Value]() { return FText::FromString(Value()); })
			.ColorAndOpacity_Lambda([Color]() { return FSlateColor(Color ? Color() : Txt); })
		]
	];
}

TSharedRef<SWidget> SSmvFlightUi::ConsoleBox()
{
	using namespace SmvFuiTheme;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const int32 N = 8;
	for (int32 i = 0; i < N; ++i)
	{
		auto Line = [this, i, N]() -> const FSmvUiLog*
		{
			const int32 Idx = St.Log.Num() - N + i;
			return St.Log.IsValidIndex(Idx) ? &St.Log[Idx] : nullptr;
		};
		Rows->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Font(Mono(8)).AutoWrapText(false)
			.Text_Lambda([Line]() { const FSmvUiLog* E = Line(); return FText::FromString(E ? FString::Printf(TEXT("%s  %s"), *E->Clock, *E->Text) : FString(TEXT(" "))); })
			.ColorAndOpacity_Lambda([Line]() { const FSmvUiLog* E = Line(); return FSlateColor(E ? Sev(E->Sev) : Dim); })
		];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 4)
		[
			SNew(STextBlock).Text(FText::FromString(Loc(TEXT("fui.comms")))).Font(Font(8, true)).ColorAndOpacity(Dim)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(&ConsoleBrush).Padding(FMargin(8.f, 6.f))
			[
				SNew(SBox).HeightOverride(122.f).WidthOverride(348.f).Clipping(EWidgetClipping::ClipToBounds)[Rows]
			]
		];
}

TSharedRef<SWidget> SSmvFlightUi::DronePanel()
{
	using namespace SmvFuiTheme;
	TSharedRef<SSmvPaintBox> Pfd = SNew(SSmvPaintBox).Size(FVector2D(348.f, 196.f)).Visibility(EVisibility::HitTestInvisible);
	Pfd->Painter = [this](const FGeometry& G, FSlateWindowElementList& Out, int32 L) { SmvPaintPfd(G, Out, L, St); };
	auto F1 = [](float V, const TCHAR* U) { return FString::Printf(TEXT("%.1f %s"), V, U); };
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(2.f));
	Grid->AddSlot(0, 0)[TeleTile(Loc(TEXT("fui.alt")), [this, F1]() { return F1(St.AltRel, TEXT("m")); })];
	Grid->AddSlot(1, 0)[TeleTile(Loc(TEXT("fui.speed")), [this]() { return FString::Printf(TEXT("%.0f km/h"), St.Gs * 3.6f); })];
	Grid->AddSlot(2, 0)[TeleTile(Loc(TEXT("fui.vs")), [this]() { return FString::Printf(TEXT("%+.1f m/s"), St.Climb); },
		[this]() { return St.Climb < -4.f ? Amber : Txt; })];
	Grid->AddSlot(3, 0)[TeleTile(Loc(TEXT("fui.hdg")), [this]() { return SmvFuiDraw::Pad3(St.Heading) + TEXT("°"); })];
	Grid->AddSlot(0, 1)[TeleTile(Loc(TEXT("fui.home")), [this]() { return St.bHasHome ? (St.DistHome >= 1000.f ? FString::Printf(TEXT("%.2f km"), St.DistHome / 1000.f) : FString::Printf(TEXT("%.0f m"), St.DistHome)) : FString(TEXT("—")); })];
	Grid->AddSlot(1, 1)[TeleTile(TEXT("GPS"), [this]() { return St.Sats >= 0 ? FSmvI18n::Format(TEXT("fui.sats"), { { TEXT("n"), FString::FromInt(St.Sats) } }).ToString() : FString(TEXT("—")); },
		[this]() { return St.GpsFix >= 3 ? Txt : Amber; })];
	Grid->AddSlot(2, 1)[TeleTile(Loc(TEXT("fui.batt")), [this]()
		{
			if (St.bElec) return FString::Printf(TEXT("%.0f%% %.1fV"), St.ElecPct, St.ElecV);
			return St.BattPct >= 0 ? FString::Printf(TEXT("%.0f%% %.1fV"), St.BattPct, St.BattV) : (St.BattV > 0 ? FString::Printf(TEXT("%.1f V"), St.BattV) : FString(TEXT("—")));
		}, [this]() { const float P = St.bElec ? St.ElecPct : St.BattPct; return P >= 0 && P < 25.f ? Red : Txt; })];
	Grid->AddSlot(3, 1)[TeleTile(Loc(TEXT("fui.thr")), [this]() { return FString::Printf(TEXT("%.0f%%"), St.Throttle); })];

	return SNew(SBox).WidthOverride(372.f)
	[
		SNew(SBorder).BorderImage(&CardBrush).Padding(0.f)
		[
			SNew(SVerticalBox)
			// başlık şeridi: durum noktası · ad · özet · ARM rozeti
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder).BorderImage(&HeadBrush).Padding(FMargin(10.f, 8.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SNew(SBox).WidthOverride(10.f).HeightOverride(10.f)
						[
							SNew(SImage).Image(&CircleBrush).ColorAndOpacity_Lambda([this]() { return FSlateColor(St.bFresh ? Green : Red); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SNew(STextBlock).Font(Font(11, true)).ColorAndOpacity(Txt).Text_Lambda([this]() { return FText::FromString(St.Title); })
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(Mono(8)).ColorAndOpacity(Dim)
						.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.1f m · %s"), St.AltRel, St.Mode.IsEmpty() ? TEXT("—") : *St.Mode)); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Chip(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(St.bArmed ? TEXT("ARM") : TEXT("SAFE")); }),
							[this]() { return St.bArmed ? Green : Dim; })
					]
				]
			]
			// uçuş göstergesi
			+ SVerticalBox::Slot().AutoHeight().Padding(12.f, 10.f, 12.f, 6.f)
			[
				Pfd
			]
			// telemetri karoları
			+ SVerticalBox::Slot().AutoHeight().Padding(10.f, 0.f, 10.f, 6.f)
			[
				Grid
			]
			// haberleşme günlüğü
			+ SVerticalBox::Slot().AutoHeight().Padding(12.f, 2.f, 12.f, 12.f)
			[
				ConsoleBox()
			]
		]
	];
}

TSharedRef<SWidget> SSmvFlightUi::Gauges()
{
	using namespace SmvFuiTheme;
	const float Big = 66.f, Small = 50.f, Gap = 12.f;
	const float W = 2 * Small + 4 * Big + 3 * Gap + 8.f, H = 2 * Big + 6.f;
	TSharedRef<SSmvPaintBox> Box = SNew(SSmvPaintBox).Size(FVector2D(W, H));
	Box->Painter = [this, Big, Small, Gap](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		const float Cy = (float)Sz.Y * 0.5f;
		float X = 4.f;
		auto Scale = [](float Max, int32 N)
		{
			TArray<FString> Lb;
			for (int32 i = 0; i <= N; ++i) Lb.Add(FString::Printf(TEXT("%.0f"), Max * i / N));
			return Lb;
		};
		// YAKIT (küçük)
		{
			FSmvDial D;
			D.Title = Loc(TEXT("fui.fuel"));
			D.bOn = St.bFuel;
			D.Frac = St.FuelPct / 100.f;
			D.RedA = 0.f; D.RedB = 0.2f;
			D.Labels = { TEXT("E"), TEXT(""), TEXT("½"), TEXT(""), TEXT("F") };
			D.Value = St.bFuel ? FString::Printf(TEXT("%.0f%%"), St.FuelPct) : FString(TEXT("—"));
			D.Sub = St.bFuel ? FString::Printf(TEXT("%.1f L"), St.FuelL) : FString();
			D.bDanger = St.bFuel && St.FuelPct < 20.f;
			SmvPaintDial(G, Out, L, FVector2D(X + Small, Cy + (Big - Small) * 0.4f), Small, D, &CircleBrush);
			X += 2 * Small + Gap;
		}
		// HIZ (büyük, km/h)
		{
			FSmvDial D;
			D.Title = Loc(TEXT("fui.speed"));
			const float Max = FMath::Max(20.f, FMath::CeilToFloat(St.MaxKmh * 1.25f / 20.f) * 20.f);
			const float V = (St.bPlane && St.As > 0 ? St.As : St.Gs) * 3.6f;
			D.Frac = V / Max;
			D.RedA = St.MaxKmh / Max; D.RedB = 1.f;
			D.Labels = Scale(Max, 4);
			D.Value = FString::Printf(TEXT("%.0f"), V);
			D.Sub = TEXT("km/h");
			D.bDanger = V > St.MaxKmh * 1.02f;
			SmvPaintDial(G, Out, L, FVector2D(X + Big, Cy), Big, D, &CircleBrush);
			X += 2 * Big + Gap;
		}
		// MOTOR (büyük, gaz %)
		{
			FSmvDial D;
			D.Title = St.bMotorCut ? Loc(TEXT("fui.motor_cut")) : Loc(TEXT("fui.motor"));
			D.bOn = St.bArmed && !St.bMotorCut;
			D.Frac = St.Throttle / 100.f;
			D.RedA = 0.9f; D.RedB = 1.f;
			D.Labels = Scale(100.f, 4);
			D.Value = FString::Printf(TEXT("%.0f%%"), St.Throttle);
			D.Sub = St.bArmed ? Loc(TEXT("fui.thr")) : FString(TEXT("SAFE"));
			SmvPaintDial(G, Out, L, FVector2D(X + Big, Cy), Big, D, &CircleBrush);
			X += 2 * Big + Gap;
		}
		// BATARYA (küçük)
		{
			FSmvDial D;
			D.Title = Loc(TEXT("fui.batt"));
			const float P = St.bElec ? St.ElecPct : St.BattPct;
			D.bOn = P >= 0.f;
			D.Frac = P / 100.f;
			D.RedA = 0.f; D.RedB = 0.25f;
			D.Labels = { TEXT("0"), TEXT(""), TEXT("50"), TEXT(""), TEXT("100") };
			D.Value = P >= 0.f ? FString::Printf(TEXT("%.0f%%"), P) : FString(TEXT("—"));
			D.Sub = St.bElec ? (St.ElecMin >= 0 ? FString::Printf(TEXT("%.1fV %.0f%s"), St.ElecV, St.ElecMin, *Loc(TEXT("fui.min"))) : FString::Printf(TEXT("%.1f V"), St.ElecV))
				: (St.BattV > 0 ? FString::Printf(TEXT("%.1f V"), St.BattV) : FString());
			D.bDanger = P >= 0.f && P < 25.f;
			SmvPaintDial(G, Out, L, FVector2D(X + Small, Cy + (Big - Small) * 0.4f), Small, D, &CircleBrush);
		}
	};
	return Box;
}

TSharedRef<SWidget> SSmvFlightUi::Sticks()
{
	using namespace SmvFuiTheme;
	auto MakeStick = [this](int32 Side) -> TSharedRef<SWidget>
	{
		TSharedRef<SSmvPaintBox> B = SNew(SSmvPaintBox).Size(FVector2D(136.f, 136.f));
		TSharedPtr<bool> Active = MakeShared<bool>(false);
		B->Painter = [this, Side, Active](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			FString Lx, Ly;
			if (St.bRover) { Lx = Side == 0 ? FString() : Loc(TEXT("fui.steer")); Ly = Side == 0 ? SmvUi::AxisName(TEXT("thr")).ToString().ToUpper() : FString(); }
			else
			{
				FString A[4];
				SmvUi::StickMap(St.StickMode, A);
				Lx = SmvUi::AxisName(A[Side * 2]).ToString().ToUpper();
				Ly = SmvUi::AxisName(A[Side * 2 + 1]).ToString().ToUpper();
			}
			SmvPaintStick(G, Out, L, Side == 0 ? St.StickL : St.StickR, *Active, Lx, Ly, &CardBrush, &CircleBrush);
		};
		B->OnDrag = [this, Side, Active](const FGeometry& G, const FVector2D& P, int32 Phase)
		{
			ASmvPilotPawn* Pw = GM.IsValid() ? GM->GetPilot() : nullptr;
			if (Phase == 2) { *Active = false; if (Pw) Pw->SetVirtualStick(Side, FVector2D::ZeroVector, false); return; }
			const FVector2D Sz = FVector2D(G.GetLocalSize());
			const float R = (float)FMath::Min(Sz.X, Sz.Y) * 0.5f - 16.f;
			const FVector2D V((P.X - Sz.X * 0.5) / R, -(P.Y - Sz.Y * 0.5) / R);
			*Active = true;
			if (Pw) Pw->SetVirtualStick(Side, V, true);
		};
		return B;
	};
	auto Ch5Seg = [this](int32 Pwm, const TCHAR* Label) -> TSharedRef<SWidget>
	{
		return SNew(SBorder).BorderImage(&ChipBrush).Padding(FMargin(7.f, 1.f))
			.BorderBackgroundColor_Lambda([this, Pwm]() { return FSlateColor(St.Ch5 == Pwm ? Accent.CopyWithNewOpacity(0.85f) : FLinearColor(1, 1, 1, 0.08f)); })
			[
				SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(7, true)).ColorAndOpacity(FLinearColor::White)
			];
	};
	return SNew(SVerticalBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[MakeStick(0)]
			+ SHorizontalBox::Slot().AutoWidth()[MakeStick(1)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(8.f, 4.f)).Visibility(EVisibility::HitTestInvisible)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("CH5 (Q/E)"))).Font(Font(7, true)).ColorAndOpacity(Dim)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 3, 0)[Ch5Seg(1000, TEXT("1"))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 3, 0)[Ch5Seg(1500, TEXT("2"))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Ch5Seg(2000, TEXT("3"))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(Mono(7)).ColorAndOpacity(Dim)
					.Text_Lambda([this]() { return FText::FromString(St.bThrUsed ? SmvUi::StickKeyHint(St.StickMode) + TEXT(" · ") + Loc(TEXT("fui.space_center")) : Loc(TEXT("fui.thr_untouched"))); })
				]
			]
		];
}

TSharedRef<SWidget> SSmvFlightUi::Feed()
{
	using namespace SmvFuiTheme;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible);
	const int32 N = 6;
	const double Life = 10.0, Fade = 2.5;
	for (int32 i = 0; i < N; ++i)
	{
		auto Line = [this, i, N, Life]() -> const FSmvUiLog*
		{
			const int32 Idx = St.Log.Num() - N + i;
			if (!St.Log.IsValidIndex(Idx)) return nullptr;
			const FSmvUiLog& E = St.Log[Idx];
			return FPlatformTime::Seconds() - E.Wall < Life ? &E : nullptr;
		};
		Rows->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SBorder)
			.BorderImage(&ChipBrush)
			.Padding(FMargin(8.f, 2.f))
			.Visibility_Lambda([Line]() { return Line() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.BorderBackgroundColor_Lambda([Line, Life, Fade]()
			{
				const FSmvUiLog* E = Line();
				const float A = E ? (float)FMath::Clamp((Life - (FPlatformTime::Seconds() - E->Wall)) / Fade, 0.0, 1.0) : 0.f;
				return FSlateColor(FLinearColor(0.02f, 0.03f, 0.04f, 0.62f * A));
			})
			[
				SNew(STextBlock).Font(Font(9, false))
				.Text_Lambda([Line]() { const FSmvUiLog* E = Line(); return FText::FromString(E ? E->Text : FString()); })
				.ColorAndOpacity_Lambda([Line, Life, Fade]()
				{
					const FSmvUiLog* E = Line();
					if (!E) return FSlateColor(FLinearColor::Transparent);
					const float A = (float)FMath::Clamp((Life - (FPlatformTime::Seconds() - E->Wall)) / Fade, 0.0, 1.0);
					return FSlateColor(Sev(E->Sev).CopyWithNewOpacity(A));
				})
			]
		];
	}
	return Rows;
}

TSharedRef<SWidget> SSmvFlightUi::BottomBar()
{
	using namespace SmvFuiTheme;
	const bool* bP = &bPanel;
	const bool* bS = &bSticks;
	return SNew(SBorder).BorderImage(&BarBrush).Padding(FMargin(8.f, 6.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			Btn(TAttribute<FText>::CreateLambda([this]()
				{
					if (St.bMotorCut) return FText::FromString(Loc(TEXT("fui.motor_on")));
					if (!St.bArmed) return FText::FromString(TEXT("ARM"));
					return FText::FromString(St.bPlane && St.AltRel > 3.f ? Loc(TEXT("fui.motor_off")) : FString(TEXT("DISARM")));
				}),
				[this]() { Act(TEXT("ArmToggle")); },
				[this]() { return St.bMotorCut ? Amber * 0.85f : St.bArmed ? Red * 0.85f : Green * 0.8f; }, 96.f)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			SNew(SBox).Visibility_Lambda([this]() { return St.bRover ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })
			[
				Btn(FText::FromString(Loc(TEXT("fui.takeoff"))), [this]() { Act(TEXT("Takeoff")); }, []() { return Blue * 0.8f; })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			Btn(FText::FromString(Loc(TEXT("fui.land"))), [this]() { Act(TEXT("Land")); }, []() { return BtnGrey; })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			Btn(FText::FromString(TEXT("RTL")), [this]() { Act(TEXT("Rtl")); }, []() { return Amber * 0.7f; })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			Btn(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(FString::Printf(TEXT("%s · %s"), *Loc(TEXT("fui.mode")), St.Mode.IsEmpty() ? TEXT("—") : *St.Mode)); }),
				[this]() { ToggleModeMenu(); }, [this]() { return bModeMenu ? Accent * 0.85f : BtnGrey; }, 150.f)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			Btn(FText::FromString(Loc(TEXT("fui.reset"))), [this]() { Act(TEXT("Reset")); }, []() { return BtnGrey; })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 3, 0)
		[
			Btn(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(FString::Printf(TEXT("%s · %s"), *Loc(TEXT("fui.camera")), *St.CamLabel)); }),
				[this]() { Act(TEXT("CamCycle")); }, []() { return BtnGrey; }, 128.f)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			Btn(FText::FromString(Loc(TEXT("fui.sticks"))), [this]() { bSticks = !bSticks; }, [bS]() { return *bS ? Accent * 0.75f : BtnGrey; })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			Btn(FText::FromString(Loc(TEXT("fui.panel"))), [this]() { bPanel = !bPanel; }, [bP]() { return *bP ? Accent * 0.75f : BtnGrey; })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			Btn(FText::FromString(Loc(TEXT("fui.menu"))), [this]() { Act(TEXT("Pause")); }, []() { return BtnGrey; })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			SNew(SBox).Visibility_Lambda([this]() { return St.bPlane ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
			[
				Btn(FText::FromString(Loc(TEXT("fui.cockpit"))), [this]() { ToggleCockpit(); }, []() { return FSmvUiPrefs::Get().bCockpit ? Accent * 0.75f : BtnGrey; })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
		[
			Btn(FText::FromString(TEXT("?")), [this]() { Act(TEXT("Help")); }, []() { return BtnGrey; }, 34.f)
		]
	];
}

TSharedRef<SWidget> SSmvFlightUi::ModeMenu()
{
	return SAssignNew(Radial, SSmvRadial)
		.Radius(230.f)
		.Items_Lambda([this]() { return St.Modes; })
		.Current_Lambda([this]() { return St.Mode; })
		.Title(FText::FromString(SmvFuiTheme::Loc(TEXT("fui.flight_mode"))))
		.Stick([this]() { return St.PadRight; })
		.OnPick([this](int32 I) { if (St.Modes.IsValidIndex(I)) Act(TEXT("Mode"), St.Modes[I]); bModeMenu = false; });
}

void SSmvFlightUi::ToggleModeMenu()
{
	bModeMenu = !bModeMenu;
	if (bModeMenu && Radial.IsValid()) Radial->ResetAnim();
	FSmvUiSound::Play(bModeMenu ? ESmvSnd::Open : ESmvSnd::Back);
}

void SSmvFlightUi::ConfirmRadial()
{
	if (!bModeMenu) return;
	if (!Radial.IsValid() || !Radial->Confirm()) { bModeMenu = false; FSmvUiSound::Play(ESmvSnd::Back); }
}

void SSmvFlightUi::ToggleCockpit()
{
	// G: kadran tasarımı döngüsü (web gtheme) — sabit kanat / VTOL'de sona klasik altılı kokpit eklenir
	FSmvUiPrefs& P = FSmvUiPrefs::Get();
	TArray<FString> Cyc = { TEXT("cockpit"), TEXT("classic"), TEXT("glass"), TEXT("minimal"), TEXT("military"), TEXT("none") };
	if (St.bPlane) Cyc.Add(TEXT("sixpack"));
	const FString Cur = (St.bPlane && P.bCockpit) ? FString(TEXT("sixpack")) : P.DialTheme;
	const FString Next = Cyc[(FMath::Max(0, Cyc.IndexOfByKey(Cur)) + 1) % Cyc.Num()];
	P.bCockpit = Next == TEXT("sixpack");
	if (!P.bCockpit) P.DialTheme = Next;
	P.Save();
	FSmvUiSound::Play(ESmvSnd::Toggle);
	if (GM.IsValid())
	{
		const FString Name = P.bCockpit ? FSmvI18n::T(TEXT("bind.cockpit")).ToString() : FSmvI18n::T(TEXT("gtheme.") + Next).ToString();
		GM->UiEvent(FSmvI18n::Format(TEXT("gtheme.changed"), { { TEXT("t"), Name } }).ToString(), 6);
	}
}

// ---------------------------------------------------------------------------
// kokpit gösterge paneli — klasik "altılı" (hız · yapay ufuk · irtifa / dönüş · yön · dikey hız)
// ---------------------------------------------------------------------------

namespace
{
	void SmvBezel(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& C, float R, const FSlateBrush* Circle)
	{
		using namespace SmvFuiDraw;
		DrRect(Out, L, G, C - FVector2D(R + 6.f, R + 6.f), FVector2D(2 * R + 12.f, 2 * R + 12.f), FLinearColor(0.10f, 0.11f, 0.13f, 0.95f), Circle);
		DrRect(Out, L, G, C - FVector2D(R, R), FVector2D(2 * R, 2 * R), FLinearColor(0.015f, 0.017f, 0.02f, 1.f), Circle);
		DrArc(Out, L + 1, G, C, R + 3.f, 0.f, 360.f, FLinearColor(0.55f, 0.58f, 0.62f, 0.8f), 1.5f);
		for (const FVector2D& S : { FVector2D(-1, -1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(1, 1) })
			DrRect(Out, L + 1, G, C + S * (R + 1.f) * 0.92f - FVector2D(2.5f, 2.5f), FVector2D(5.f, 5.f), FLinearColor(0.35f, 0.37f, 0.4f, 1.f), Circle);   // vida
	}

	void SmvNeedle(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& C, float AngDeg, float Len, float W, const FLinearColor& Col, const FSlateBrush* Circle)
	{
		using namespace SmvFuiDraw;
		const float A = FMath::DegreesToRadians(AngDeg);
		const FVector2D U(FMath::Cos(A), FMath::Sin(A));
		DrSeg(Out, L, G, C - U * Len * 0.18f, C + U * Len, FLinearColor(0, 0, 0, 0.5f), W + 2.f);
		DrSeg(Out, L + 1, G, C - U * Len * 0.18f, C + U * Len, Col, W);
		DrRect(Out, L + 2, G, C - FVector2D(5, 5), FVector2D(10, 10), FLinearColor(0.2f, 0.2f, 0.22f, 1.f), Circle);
	}

	/** Kadran ölçeği: Ang0 başlangıç, Sweep süpürme (derece), N ana çentik, etiketler */
	void SmvScale(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FVector2D& C, float R, float Ang0, float Sweep, int32 N, int32 Minor, const TArray<FString>& Labels)
	{
		using namespace SmvFuiDraw;
		using namespace SmvFuiTheme;
		const FLinearColor W(0.95f, 0.95f, 0.95f, 1.f);
		for (int32 i = 0; i <= N * Minor; ++i)
		{
			const float A = FMath::DegreesToRadians(Ang0 + Sweep * i / (N * Minor));
			const FVector2D U(FMath::Cos(A), FMath::Sin(A));
			const bool bMaj = i % Minor == 0;
			DrSeg(Out, L, G, C + U * (R - 2.f), C + U * (R - (bMaj ? 11.f : 6.f)), W, bMaj ? 2.f : 1.f);
			if (bMaj && Labels.IsValidIndex(i / Minor)) DrText(Out, L, G, Labels[i / Minor], C + U * (R - 21.f), Font(8, true), W, 0);
		}
	}
}

TSharedRef<SWidget> SSmvFlightUi::Cockpit()
{
	const float R = 60.f, Gap = 16.f;
	const float W = 6 * (2 * R + 12.f) + 5 * Gap + 24.f, H = 2 * R + 12.f + 34.f;
	TSharedRef<SSmvPaintBox> Box = SNew(SSmvPaintBox).Size(FVector2D(W, H));
	Box->Painter = [this, R, Gap](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		using namespace SmvFuiDraw;
		using namespace SmvFuiTheme;
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		DrRect(Out, L, G, FVector2D::ZeroVector, Sz, FLinearColor(0.035f, 0.04f, 0.05f, 0.88f), &CardBrush);
		const FLinearColor Wt(0.95f, 0.95f, 0.95f, 1.f), Org(1.f, 0.55f, 0.1f, 1.f);
		const float Cy = R + 6.f + 10.f;
		float X = 12.f + R + 6.f;
		auto Next = [&X, R, Gap]() { const float C = X; X += 2 * R + 12.f + Gap; return C; };
		auto Caption = [&](float Cx, const FString& T) { DrText(Out, L + 9, G, T, FVector2D(Cx, Sz.Y - 12.f), Font(7, true), Dim, 0); };
		// 1) hava hızı (km/h)
		{
			const FVector2D C(Next(), Cy);
			SmvBezel(Out, L + 1, G, C, R, &CircleBrush);
			const float Max = FMath::Max(60.f, FMath::CeilToFloat(St.MaxKmh * 1.3f / 20.f) * 20.f);
			const float A0 = -90.f, Sw = 320.f;
			auto Ang = [A0, Sw, Max](float V) { return A0 + Sw * FMath::Clamp(V / Max, 0.f, 1.f); };
			DrArc(Out, L + 2, G, C, R - 4.f, Ang(Max * 0.25f), Ang(St.MaxKmh * 0.85f), Green, 4.f);
			DrArc(Out, L + 2, G, C, R - 4.f, Ang(St.MaxKmh * 0.85f), Ang(St.MaxKmh), Amber, 4.f);
			const float Ra = FMath::DegreesToRadians(Ang(St.MaxKmh));
			DrSeg(Out, L + 2, G, C + FVector2D(FMath::Cos(Ra), FMath::Sin(Ra)) * (R - 2.f), C + FVector2D(FMath::Cos(Ra), FMath::Sin(Ra)) * (R - 14.f), Red, 3.f);
			TArray<FString> Lb;
			for (int32 i = 0; i <= 8; ++i) Lb.Add(FString::Printf(TEXT("%.0f"), Max * i / 8));
			SmvScale(Out, L + 3, G, C, R, A0, Sw, 8, 2, Lb);
			DrText(Out, L + 3, G, TEXT("KM/H"), C + FVector2D(0, R * 0.35f), Font(7, true), Dim, 0);
			SmvNeedle(Out, L + 4, G, C, Ang((St.As > 0 ? St.As : St.Gs) * 3.6f), R - 8.f, 3.f, Wt, &CircleBrush);
			Caption((float)C.X, Loc(TEXT("fui.ck_asi")));
		}
		// 2) yapay ufuk
		{
			const FVector2D C(Next(), Cy);
			SmvBezel(Out, L + 1, G, C, R, &CircleBrush);
			const float Ri = R - 3.f;
			const FLinearColor Sky(0.16f, 0.45f, 0.82f, 1.f), Gnd(0.47f, 0.30f, 0.14f, 1.f);
			DrRect(Out, L + 2, G, C - FVector2D(Ri, Ri), FVector2D(2 * Ri, 2 * Ri), Sky, &CircleBrush);
			const float Ppd = Ri / 30.f;
			const FVector2D N = DrRot(FVector2D(0, 1), -St.Roll);
			const FVector2D H0 = C + DrRot(FVector2D(0, FMath::RadiansToDegrees(St.Pitch) * Ppd), -St.Roll);
			for (float y = -Ri; y < Ri; y += 2.f)
			{
				const float Yc = (float)C.Y + y + 1.f;
				const float Hw = FMath::Sqrt(FMath::Max(0.f, Ri * Ri - (y + 1.f) * (y + 1.f)));
				float X0 = (float)C.X - Hw, X1 = (float)C.X + Hw;
				if (FMath::Abs(N.X) < 1e-4) { if ((Yc - H0.Y) * N.Y <= 0.f) continue; }
				else
				{
					const float Xs = (float)(H0.X - (Yc - H0.Y) * N.Y / N.X);
					if (N.X > 0) X0 = FMath::Max(X0, Xs); else X1 = FMath::Min(X1, Xs);
					if (X1 <= X0) continue;
				}
				DrSeg(Out, L + 3, G, FVector2D(X0, Yc), FVector2D(X1, Yc), Gnd, 2.6f);
			}
			const FVector2D Dir = DrRot(FVector2D(1, 0), -St.Roll);
			const float Hl = FMath::Sqrt(FMath::Max(0.f, Ri * Ri - (float)(H0 - C).SizeSquared()));
			if (Hl > 0.f) DrSeg(Out, L + 4, G, H0 - Dir * Hl, H0 + Dir * Hl, Wt, 1.5f);
			for (int32 Deg : { -20, -10, 10, 20 })
			{
				const float Off = (FMath::RadiansToDegrees(St.Pitch) - Deg) * Ppd;
				if (FMath::Abs(Off) > Ri * 0.7f) continue;
				const float Hw = Deg % 20 == 0 ? 16.f : 9.f;
				DrSeg(Out, L + 4, G, C + DrRot(FVector2D(-Hw, Off), -St.Roll), C + DrRot(FVector2D(Hw, Off), -St.Roll), Wt, 1.2f);
			}
			DrArc(Out, L + 4, G, C, Ri - 6.f, -150.f, -30.f, Wt, 1.2f);
			const float Pa = FMath::DegreesToRadians(-90.f) - St.Roll;
			const FVector2D Up(FMath::Cos(Pa), FMath::Sin(Pa)), Tn(-Up.Y, Up.X);
			const FVector2D Tip = C + Up * (Ri - 7.f);
			DrPoly(Out, L + 5, G, { Tip, Tip - Up * 8.f + Tn * 5.f, Tip - Up * 8.f - Tn * 5.f, Tip }, Org, 2.f);
			DrPoly(Out, L + 6, G, { C + FVector2D(-34, 0), C + FVector2D(-12, 0), C + FVector2D(-6, 6), C + FVector2D(0, 0), C + FVector2D(6, 6), C + FVector2D(12, 0), C + FVector2D(34, 0) }, Org, 3.f);
			Caption((float)C.X, Loc(TEXT("fui.ck_att")));
		}
		// 3) altimetre (m; büyük ibre 100 m, küçük ibre 1000 m)
		{
			const FVector2D C(Next(), Cy);
			SmvBezel(Out, L + 1, G, C, R, &CircleBrush);
			TArray<FString> Lb;
			for (int32 i = 0; i < 10; ++i) Lb.Add(FString::FromInt(i));
			Lb.Add(FString());
			SmvScale(Out, L + 3, G, C, R, -90.f, 360.f, 10, 5, Lb);
			const float A = FMath::Max(0.f, St.AltRel);
			DrRect(Out, L + 3, G, C + FVector2D(-22.f, R * 0.22f), FVector2D(44.f, 15.f), FLinearColor(0, 0, 0, 1), nullptr);
			DrText(Out, L + 4, G, FString::Printf(TEXT("%05.0f"), A), C + FVector2D(0, R * 0.22f + 7.5f), Mono(8), Wt, 0);
			SmvNeedle(Out, L + 5, G, C, -90.f + 360.f * FMath::Fmod(A / 1000.f, 1.f), R * 0.48f, 4.f, Wt, &CircleBrush);
			SmvNeedle(Out, L + 6, G, C, -90.f + 360.f * FMath::Fmod(A / 100.f, 1.f), R - 9.f, 2.5f, Wt, &CircleBrush);
			Caption((float)C.X, Loc(TEXT("fui.ck_alt")));
		}
		// 4) dönüş göstergesi (standart dönüş 3°/s → 15° yatış simgesi)
		{
			const FVector2D C(Next(), Cy);
			SmvBezel(Out, L + 1, G, C, R, &CircleBrush);
			for (float M : { -15.f, 0.f, 15.f })
			{
				const float A = FMath::DegreesToRadians(180.f + M), B = FMath::DegreesToRadians(-M);
				DrSeg(Out, L + 3, G, C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (R - 4.f), C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (R - 14.f), Wt, 2.f);
				DrSeg(Out, L + 3, G, C + FVector2D(FMath::Cos(B), FMath::Sin(B)) * (R - 4.f), C + FVector2D(FMath::Cos(B), FMath::Sin(B)) * (R - 14.f), Wt, 2.f);
			}
			DrText(Out, L + 3, G, TEXT("L"), C + FVector2D(-R + 18.f, 18.f), Font(8, true), Wt, 0);
			DrText(Out, L + 3, G, TEXT("R"), C + FVector2D(R - 18.f, 18.f), Font(8, true), Wt, 0);
			const float Bank = FMath::DegreesToRadians(FMath::Clamp(TurnRate / 3.f * 15.f, -40.f, 40.f));
			auto P = [&C, Bank](float x, float y) { return C + DrRot(FVector2D(x, y), Bank); };
			DrPoly(Out, L + 4, G, { P(-R + 16.f, 0), P(R - 16.f, 0) }, Wt, 3.f);
			DrPoly(Out, L + 4, G, { P(0, -10), P(0, 8) }, Wt, 3.f);
			DrPoly(Out, L + 4, G, { P(-10, 8), P(10, 8) }, Wt, 2.f);
			DrRect(Out, L + 3, G, C + FVector2D(-26.f, R * 0.45f), FVector2D(52.f, 12.f), FLinearColor(0.15f, 0.15f, 0.12f, 1.f), &ChipBrush);
			DrRect(Out, L + 4, G, C + FVector2D(-6.f, R * 0.45f), FVector2D(12.f, 12.f), FLinearColor(0.05f, 0.05f, 0.05f, 1.f), &CircleBrush);
			DrText(Out, L + 4, G, FString::Printf(TEXT("%+.1f°/s"), TurnRate), C + FVector2D(0, -R * 0.42f), Mono(7), Dim, 0);
			Caption((float)C.X, Loc(TEXT("fui.ck_turn")));
		}
		// 5) yön göstergesi (dönen pusula kartı)
		{
			const FVector2D C(Next(), Cy);
			SmvBezel(Out, L + 1, G, C, R, &CircleBrush);
			for (int32 d = 0; d < 360; d += 5)
			{
				const float A = FMath::DegreesToRadians(-90.f + d - St.Heading);
				const FVector2D U(FMath::Cos(A), FMath::Sin(A));
				const bool bMaj = d % 10 == 0;
				DrSeg(Out, L + 3, G, C + U * (R - 3.f), C + U * (R - (bMaj ? 11.f : 6.f)), Wt, bMaj ? 1.6f : 1.f);
				if (d % 30 == 0)
				{
					const FString Lb = d == 0 ? TEXT("N") : d == 90 ? TEXT("E") : d == 180 ? TEXT("S") : d == 270 ? TEXT("W") : FString::FromInt(d / 10);
					DrText(Out, L + 3, G, Lb, C + U * (R - 21.f), Font(d % 90 == 0 ? 9 : 7, true), d == 0 ? Org : Wt, 0);
				}
			}
			DrPoly(Out, L + 5, G, { C + FVector2D(0, -R + 2.f), C + FVector2D(-5, -R + 12.f), C + FVector2D(5, -R + 12.f), C + FVector2D(0, -R + 2.f) }, Org, 2.f);
			DrPoly(Out, L + 5, G, { C + FVector2D(0, -16), C + FVector2D(0, 16) }, Org, 3.f);
			DrPoly(Out, L + 5, G, { C + FVector2D(-15, -2), C + FVector2D(15, -2) }, Org, 3.f);
			DrPoly(Out, L + 5, G, { C + FVector2D(-7, 13), C + FVector2D(7, 13) }, Org, 2.f);
			Caption((float)C.X, Loc(TEXT("fui.ck_hdg")));
		}
		// 6) dikey hız (m/s; 0 sol yatay)
		{
			const FVector2D C(Next(), Cy);
			SmvBezel(Out, L + 1, G, C, R, &CircleBrush);
			auto Ang = [](float V) { return 180.f + FMath::Clamp(V / 10.f, -1.f, 1.f) * 170.f; };
			for (int32 v = -10; v <= 10; ++v)
			{
				const float A = FMath::DegreesToRadians(Ang((float)v));
				const FVector2D U(FMath::Cos(A), FMath::Sin(A));
				const bool bMaj = v % 5 == 0;
				DrSeg(Out, L + 3, G, C + U * (R - 3.f), C + U * (R - (bMaj ? 11.f : 6.f)), Wt, bMaj ? 2.f : 1.f);
				if (bMaj) DrText(Out, L + 3, G, FString::FromInt(FMath::Abs(v)), C + U * (R - 21.f), Font(8, true), Wt, 0);
			}
			DrText(Out, L + 3, G, TEXT("UP"), C + FVector2D(-R * 0.35f, -R * 0.3f), Font(6, true), Dim, 0);
			DrText(Out, L + 3, G, TEXT("DN"), C + FVector2D(-R * 0.35f, R * 0.3f), Font(6, true), Dim, 0);
			DrText(Out, L + 3, G, TEXT("M/S"), C + FVector2D(R * 0.3f, 0), Font(7, true), Dim, 0);
			SmvNeedle(Out, L + 4, G, C, Ang(-St.Climb), R - 8.f, 3.f, Wt, &CircleBrush);
			Caption((float)C.X, Loc(TEXT("fui.ck_vsi")));
		}
	};
	return Box;
}

// ---------------------------------------------------------------------------
// giriş sekansı 3-2-1-BAŞLA ve ilk uçuş ipuçları
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvFlightUi::Intro()
{
	TSharedRef<SSmvPaintBox> Box = SNew(SSmvPaintBox).Size(FVector2D(360.f, 230.f)).Visibility(EVisibility::HitTestInvisible);
	Box->Painter = [this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		using namespace SmvFuiDraw;
		using namespace SmvFuiTheme;
		if (!St.bIntroAllowed || !St.bTele || St.FlightS > 3.7 || St.bMenuOpen) return;
		const float T = (float)St.FlightS;
		const int32 Sec = T < 1.f ? 3 : T < 2.f ? 2 : T < 3.f ? 1 : 0;
		const float K = Sec > 0 ? FMath::Fmod(T, 1.f) : (T - 3.f) / 0.7f;
		const FVector2D C(G.GetLocalSize().X * 0.5f, 92.f);
		const float Pop = 1.f - FMath::Pow(1.f - FMath::Clamp(K * 4.f, 0.f, 1.f), 3.f);
		const float A = Sec > 0 ? 1.f - FMath::Max(0.f, K - 0.75f) * 4.f : 1.f - K;
		SmvUi::DrGlow(Out, L, G, C, 90.f * Pop, Accent.CopyWithNewOpacity(0.35f * A));
		DrArc(Out, L + 1, G, C, 70.f, 0.f, 360.f, FLinearColor(1, 1, 1, 0.15f * A), 3.f);
		DrArc(Out, L + 2, G, C, 70.f, -90.f, -90.f + 360.f * FMath::Clamp(T / 3.f, 0.f, 1.f), Accent.CopyWithNewOpacity(A), 4.f);
		const FString S = Sec > 0 ? FString::FromInt(Sec) : Loc(TEXT("fui.go"));
		DrText(Out, L + 3, G, S, C, Font(Sec > 0 ? 64 : 34, true), (Sec > 0 ? FLinearColor::White : Green).CopyWithNewOpacity(A * Pop), 0);
		DrText(Out, L + 3, G, St.TrackName, C + FVector2D(0, 100.f), Font(12, true), Dim.CopyWithNewOpacity(A), 0);
	};
	return Box;
}

TSharedRef<SWidget> SSmvFlightUi::Tips()
{
	using namespace SmvFuiTheme;
	auto K = [](const TCHAR* Action) { return SmvUi::KeyLabel(FSmvUiPrefs::Get().KeyFor(FName(Action), false)); };
	const FString Line = FSmvI18n::Format(TEXT("fui.tips"), {
		{ TEXT("arm"), K(TEXT("ArmToggle")) }, { TEXT("takeoff"), K(TEXT("Takeoff")) }, { TEXT("mode"), K(TEXT("ModeMenu")) },
		{ TEXT("cam"), K(TEXT("CamCycle")) }, { TEXT("help"), K(TEXT("Help")) } }).ToString();
	return SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(16.f, 10.f))
		.Visibility_Lambda([this]() { return bTips && !St.bMenuOpen ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[SmvUi::Icon(ESmvIcon::Info, 22.f, Accent)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Loc(TEXT("fui.tips_title")))).Font(Font(10, true)).ColorAndOpacity(Accent)]
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Line)).Font(Font(10)).ColorAndOpacity(Txt)]
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(SmvUi::StickKeyHint(St.StickMode))).Font(Mono(8)).ColorAndOpacity(Dim)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 0, 0)
			[
				Btn(FText::FromString(TEXT("×")), [this]() { bTips = false; FSmvUiPrefs::Get().bFlightTipsDone = true; FSmvUiPrefs::Get().Save(); }, []() { return BtnGrey; }, 30.f)
			]
		];
}

// ---------------------------------------------------------------------------
// rüzgâr göstergesi (web flightsim_wind: pusula, akış oku, hız/yön, baş-kuyruk / yan bileşen)
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvFlightUi::WindCard()
{
	TSharedRef<SSmvPaintBox> Box = SNew(SSmvPaintBox).Size(FVector2D(232.f, 78.f)).Visibility(EVisibility::HitTestInvisible);
	Box->Painter = [this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		using namespace SmvFuiDraw;
		using namespace SmvFuiTheme;
		if (St.bMenuOpen) return;
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		DrRect(Out, L, G, FVector2D::ZeroVector, Sz, FLinearColor::White, &CardBrush);
		const FVector2D C(39.f, Sz.Y * 0.5f);
		const float R = 30.f;
		DrArc(Out, L + 1, G, C, R, 0.f, 360.f, Edge, 1.5f);
		// pusula: araç yönü yukarı (dönen kart)
		static const TCHAR* Card[] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };
		for (int32 k = 0; k < 4; ++k)
		{
			const float A = FMath::DegreesToRadians(-90.f + k * 90.f - St.Heading);
			DrText(Out, L + 1, G, Card[k], C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (R - 8.f), Font(7, true), k == 0 ? Amber : Dim, 0);
		}
		// akış oku: rüzgârın gittiği yön
		const float To = FMath::DegreesToRadians(St.WindDir + 180.f - 90.f - St.Heading);
		const FVector2D U(FMath::Cos(To), FMath::Sin(To)), Tn(-U.Y, U.X);
		const float Len = R * FMath::Clamp(0.35f + St.WindMs / 15.f * 0.5f, 0.35f, 0.85f);
		DrSeg(Out, L + 2, G, C - U * Len, C + U * Len, Accent, 2.5f);
		DrPoly(Out, L + 2, G, { C + U * Len - U * 8.f + Tn * 5.f, C + U * Len, C + U * Len - U * 8.f - Tn * 5.f }, Accent, 2.5f);
		// bileşenler (rüzgâr nereden: baş + / kuyruk −; yan: sağdan +)
		const float Rel = FMath::DegreesToRadians(St.WindDir - St.Heading);
		const float Head = St.WindMs * FMath::Cos(Rel), Cross = St.WindMs * FMath::Sin(Rel);
		const float X = 80.f;
		DrText(Out, L + 3, G, FString::Printf(TEXT("%.1f m/s  %03.0f°"), St.WindMs, FMath::Fmod(St.WindDir + 360.f, 360.f)), FVector2D(X, 16.f), Mono(10), Txt, -1);
		DrText(Out, L + 3, G, St.WindGust > 0.05f ? FString::Printf(TEXT("%s %.1f"), *Loc(TEXT("fui.gust")), St.WindGust) : Loc(TEXT("fui.wind")), FVector2D(X, 34.f), Font(8, true), Dim, -1);
		DrText(Out, L + 3, G, FString::Printf(TEXT("%s %+.1f · %s %+.1f"), *Loc(Head >= 0.f ? TEXT("fui.headwind") : TEXT("fui.tailwind")), FMath::Abs(Head),
			*Loc(TEXT("fui.crosswind")), Cross), FVector2D(X, 54.f), Mono(8), FMath::Abs(Cross) > 6.f ? Amber : Txt, -1);
	};
	return Box;
}

// ---------------------------------------------------------------------------
// FPV parkuru şeridi (web fpv_race HUD): tur, tur süresi, en iyi tur, sıradaki kapı + mesafe, sektör kutuları, toplam, ceza, hayalet farkı
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SSmvFlightUi::FpvBar()
{
	TSharedRef<SSmvPaintBox> Box = SNew(SSmvPaintBox).Size(FVector2D(700.f, 82.f)).Visibility(EVisibility::HitTestInvisible);
	Box->Painter = [this](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
	{
		using namespace SmvFuiDraw;
		using namespace SmvFuiTheme;
		if (!St.bFpv || St.bMenuOpen) return;
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		DrRect(Out, L, G, FVector2D::ZeroVector, Sz, FLinearColor::White, &CardBrush);
		auto Tm = [](float S) { return S < 0.f ? FString(TEXT("—")) : FString::Printf(TEXT("%d:%05.2f"), (int32)(S / 60.f), FMath::Fmod(S, 60.f)); };
		DrText(Out, L + 1, G, FString::Printf(TEXT("%s %d/%d"), *Loc(TEXT("fpv.lap")), FMath::Max(1, St.FpvLap), St.FpvLaps), FVector2D(16.f, 18.f), Font(9, true), Dim, -1);
		DrText(Out, L + 1, G, St.bFpvFinished ? Tm(St.FpvTotal) : Tm(St.FpvLapT), FVector2D(16.f, 44.f), Mono(18), Txt, -1);
		DrText(Out, L + 1, G, FString::Printf(TEXT("%s %s"), *Loc(TEXT("fpv.best")), *Tm(St.FpvBest)), FVector2D(16.f, 68.f), Mono(8), Accent, -1);
		const float Cx = (float)Sz.X * 0.5f;
		DrText(Out, L + 1, G, St.bFpvFinished ? Loc(TEXT("fpv.completed")) : FSmvI18n::Format(TEXT("fpv.next_gate"), { { TEXT("i"), FString::FromInt(St.FpvNext) }, { TEXT("n"), FString::FromInt(St.FpvTotalCp) } }).ToString(),
			FVector2D(Cx, 18.f), Font(10, true), Green, 0);
		if (!St.bFpvFinished) DrText(Out, L + 1, G, FString::Printf(TEXT("%.0f m"), St.FpvDist), FVector2D(Cx, 38.f), Mono(10), Txt, 0);
		const int32 Ns = FMath::Max(1, St.FpvSectorCount);
		const float Bw = 34.f, Gap = 4.f, X0 = Cx - (Ns * Bw + (Ns - 1) * Gap) * 0.5f;
		for (int32 k = 0; k < Ns; ++k)
		{
			const int32 Cc = St.FpvSectors.IsValidIndex(k) ? St.FpvSectors[k] : 0;
			const FLinearColor Col = Cc == 1 ? FLinearColor(0.66f, 0.33f, 0.97f) : Cc == 2 ? Green : Cc == 3 ? Amber : FLinearColor(1, 1, 1, 0.12f);
			DrRect(Out, L + 1, G, FVector2D(X0 + k * (Bw + Gap), 54.f), FVector2D(Bw, 14.f), Col, &ChipBrush);
			DrText(Out, L + 2, G, FString::Printf(TEXT("S%d"), k + 1), FVector2D(X0 + k * (Bw + Gap) + Bw * 0.5f, 61.f), Font(7, true), FLinearColor(0.05f, 0.05f, 0.08f), 0);
		}
		const float Rx = (float)Sz.X - 16.f;
		DrText(Out, L + 1, G, FString::Printf(TEXT("%s %s"), *Loc(TEXT("fpv.total")), *Tm(St.FpvTotal)), FVector2D(Rx, 18.f), Mono(9), Txt, 1);
		DrText(Out, L + 1, G, FString::Printf(TEXT("%s +%.0f s · %d / %d"), *Loc(TEXT("fpv.pen")), St.FpvPen, St.FpvMisses, St.FpvHits), FVector2D(Rx, 40.f), Mono(8), St.FpvPen > 0.f ? Amber : Dim, 1);
		if (St.FpvGhost < 1e8f)
			DrText(Out, L + 1, G, FString::Printf(TEXT("%s %+.2f"), *Loc(TEXT("fpv.ghost_hud")), St.FpvGhost), FVector2D(Rx, 64.f), Mono(10), St.FpvGhost <= 0.f ? Green : Red, 1);
	};
	return Box;
}
